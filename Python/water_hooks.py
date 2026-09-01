"""Splices a project's own material functions into the generated masters.

A generated material cannot be hand-edited and keep the edit: the next authoring run empties the
graph and rebuilds it. A hook is the way round that. Project Settings -> Mob Water names a function
and a point, and the generator wires it in every time.

Wiring is by name and nothing else. The master hands this a dict of live signals - Color, Opacity,
Foam, Column, and so on; a hook function's declared inputs are looked up in it, and its declared
outputs replace the entries of the same name. A signal the function has no output for passes
through untouched, so a function that reads five things and writes one costs nothing on the other
four.

Call read() once per authoring run, then apply() at each point.
"""

import unreal

import mob_water_graph as g

# Point names, matching EMobWaterHookPoint. See the enum for what is live at each.
OUTPUT = 'Output'
WPO = 'WorldPositionOffset'

POINTS = [OUTPUT, WPO]

# Filled by read(). Each entry is a dict: function, point, switch, switch_default, switch_group,
# masters.
HOOKS = []


def _log(msg):
    unreal.log('[MobWater] ' + str(msg))


def point_from_enum(value):
    """The point name for an EMobWaterHookPoint.

    An enum stringifies as "<MobWaterHookPoint.WORLD_POSITION_OFFSET: 1>", so match on the UHT-cased
    name appearing rather than on either end of it.
    """
    text = str(value).upper()
    for name in POINTS:
        uht = ''.join(('_' + c if c.isupper() and i else c) for i, c in enumerate(name)).upper()
        if uht in text:
            return name
    return None


def read():
    """Reads the hook list off the project settings. Call once, before authoring anything."""
    global HOOKS
    HOOKS = []

    settings_class = getattr(unreal, 'MobWaterSettings', None)
    if settings_class is None:
        # The plugin's binary predates the hook settings. Splicing nothing is the right answer;
        # failing here would take the whole authoring run down with it.
        _log('MobWaterSettings not in the loaded binary, no hooks')
        return HOOKS

    settings = unreal.get_default_object(settings_class)
    for entry in (settings.get_editor_property('hooks') or []):
        function = entry.get_editor_property('function')
        if function is None:
            continue
        point = point_from_enum(entry.get_editor_property('point'))
        if point is None:
            _log('hook %s: unknown point, skipped' % function.get_name())
            continue
        switch = str(entry.get_editor_property('switch_name') or '')
        HOOKS.append({
            'function': function,
            'point': point,
            # An unset FName reads back as "None", which is a perfectly good parameter name and so
            # cannot be told from one an artist typed by asking the string.
            'switch': '' if switch in ('', 'None') else switch,
            'switch_default': bool(entry.get_editor_property('switch_default')),
            'switch_group': str(entry.get_editor_property('switch_group') or 'Project'),
            'masters': [str(m) for m in (entry.get_editor_property('masters') or [])],
        })

    if HOOKS:
        _log('%d hook(s) from project settings' % len(HOOKS))
    return HOOKS


def declared_pins(function):
    """The input and output names a material function declares, read off its own graph.

    Read from the asset rather than from a call node: a freshly created call has not necessarily
    populated its pin arrays yet, and an empty read there would silently wire nothing.
    """
    ins, outs = [], []
    for e in g.MEL.get_material_function_expressions(function):
        if isinstance(e, unreal.MaterialExpressionFunctionInput):
            ins.append(str(e.get_editor_property('input_name')))
        elif isinstance(e, unreal.MaterialExpressionFunctionOutput):
            outs.append(str(e.get_editor_property('output_name')))
    return ins, outs


def _static_switch_param(mat, name, group, default, true_src, true_out, false_src, false_out, x, y):
    sw = g.expr(mat, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    sw.set_editor_property('parameter_name', name)
    sw.set_editor_property('default_value', bool(default))
    sw.set_editor_property('group', str(group))
    g.link(true_src, true_out, sw, 'True')
    g.link(false_src, false_out, sw, 'False')
    return sw


def apply(mat, point, signals, x=0, y=0):
    """Splices every hook sitting at this point, and returns the signals as they leave it.

    signals maps a name to an (expression, output name) pair. The dict is copied rather than
    written through, so a caller holding the previous one still sees what it had.
    """
    master = mat.get_name()
    at = [h for h in HOOKS
          if h.get('point') == point and (not h['masters'] or master in h['masters'])]
    if not at:
        return signals

    signals = dict(signals)
    _log('%s hooks at %s: %s' % (master, point, ', '.join(h['function'].get_name() for h in at)))
    _log('  signals: ' + ', '.join(sorted(signals)))

    for index, hook in enumerate(at):
        function = hook['function']
        row = y + index * 4
        call = g.expr(mat, unreal.MaterialExpressionMaterialFunctionCall, x, row)
        call.set_editor_property('material_function', function)

        ins, outs = declared_pins(function)

        # An input naming nothing live is left on the function's own default rather than treated as
        # an error: a project function is written once and spliced at whichever point suits, and the
        # pools differ.
        unmatched = []
        for name in ins:
            source = signals.get(name)
            if source is None:
                unmatched.append(name)
                continue
            g.link(source[0], source[1], call, name)
        if unmatched:
            _log('  %s: no signal for %s' % (function.get_name(), ', '.join(unmatched)))

        switch = str(hook.get('switch') or '')
        group = hook.get('switch_group') or 'Project'
        default = bool(hook.get('switch_default'))

        for offset, name in enumerate(outs):
            previous = signals.get(name)

            # World position offset is added by the caller rather than replacing a signal, so it is
            # the one output allowed to name something that was not already there.
            if previous is None and not (point == WPO and name == WPO):
                _log('  %s: output %s replaces nothing at %s, ignored'
                     % (function.get_name(), name, point))
                continue

            source = (call, name)
            if switch:
                false_src = previous if previous is not None else (g.const3(mat, (0.0, 0.0, 0.0),
                                                                            x + 1, row), '')
                source = (_static_switch_param(mat, switch, group, default, call, name,
                                               false_src[0], false_src[1],
                                               x + 2, row + offset), '')
            signals[name] = source

    return signals
