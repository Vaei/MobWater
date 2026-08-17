# Copyright (c) Jared Taylor. All Rights Reserved

"""Asserts the claims MobWater makes, one at a time, so a failure names the claim that stopped
being true.

The wave maths exists twice - Source/MobWater/Public/MobWaterWaves.h and
Shaders/Public/MobWaterWaves.ush - and nothing in the build keeps the two equal. This is what keeps
them equal.

Two halves, and they check different kinds of drift:

- The constants and the custom primitive data contract are read out of the source itself and
  compared. That catches an edit to one side that never reached the other, which is the failure that
  produces water that is subtly wrong everywhere rather than obviously wrong somewhere.

- Numeric parity between the compiled shader and the C++ needs a material to evaluate and a render
  target to read back, so it arrives with the master material rather than here. Until then this
  script says so rather than reporting a pass it did not earn.

- The generated content carries the plugin version that built it, which catches the drift the other
  two cannot see: content that is internally consistent and was simply built by an older plugin.
"""

import importlib
import math
import os
import re

try:
    import unreal
except ImportError:
    unreal = None

import mob_water_version


PLUGIN_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HEADER = os.path.join(PLUGIN_ROOT, 'Source', 'MobWater', 'Public', 'MobWaterWaves.h')
SHADER = os.path.join(PLUGIN_ROOT, 'Shaders', 'Public', 'MobWaterWaves.ush')
TYPES = os.path.join(PLUGIN_ROOT, 'Source', 'MobWater', 'Public', 'MobWaterTypes.h')


# The custom primitive data layout, as the README and the generator both understand it.
#
# This table is the contract and the header is checked against it rather than the other way round -
# a typo in the header should fail, not redefine the contract.
EXPECTED_INDICES = {
    'ShallowColor': 0,
    'DeepColor': 3,
    'FadeDepth': 6,
    'ClarityDepth': 7,
    'ShoreFoamDepth': 8,
    'CrestFoamThreshold': 9,
    'WaveAmplitude': 10,
    'ShoreFadeDistance': 11,
    'RefractionStrength': 12,
    'RippleStrength': 13,
    'Roughness': 14,
    'FlowVelocity': 15,
    'HalfExtent': 17,
    'DetailStrength': 19,
    'CausticStrength': 20,
    'CausticDepth': 21,
    'FoamNoiseAmount': 22,
    'FoamSharpness': 23,
    'FoamBands': 24,
    'GlintGloss': 25,
    'GlintStrength': 26,
    'GlintThreshold': 27,
    'DetailScrollSpeed': 28,
    'MacroStrength': 29,
    'EdgeFoamWidth': 30,
    'MinOpacity': 31,
    'ReflectionStrength': 32,
    'GlintDensity': 33,
    'GlintEmissive': 34,
    'Unlit': 35,
    'Num': 36,
}

# The engine's FCustomPrimitiveData is nine float4s (SceneTypes.h). A write past the end is not an
# error, it is a no-op, so a contract that overruns this leaves parameters that quietly do nothing.
CUSTOM_PRIMITIVE_DATA_FLOATS = 36


def _log(message):
    if unreal:
        unreal.log('[MobWater] {0}'.format(message))
    else:
        print('[MobWater] {0}'.format(message))


def _log_error(message):
    if unreal:
        unreal.log_error('[MobWater] {0}'.format(message))
    else:
        print('[MobWater] ERROR {0}'.format(message))


def _read(path):
    with open(path, 'r', encoding='utf-8') as handle:
        return handle.read()


def _cpp_constant(text, name):
    """Value of `static constexpr <type> <name> = <value>;`, as a float."""
    match = re.search(r'static\s+constexpr\s+\w+\s+' + name + r'\s*=\s*([0-9.eE+-]+)f?\s*;', text)
    return float(match.group(1)) if match else None


def _hlsl_define(text, name):
    """Value of `#define <name> <value>`, as a float."""
    match = re.search(r'#define\s+' + name + r'\s+([0-9.eE+-]+)f?\s*$', text, re.MULTILINE)
    return float(match.group(1)) if match else None


def check_constants():
    """The numbers the two implementations are both written against."""
    header = _read(HEADER)
    shader = _read(SHADER)

    pairs = [
        ('Gravity', 'Gravity', 'MOB_WATER_GRAVITY'),
        ('MaxWaves', 'MaxWaves', 'MOB_WATER_MAX_WAVES'),
        ('MaxSteepness', 'MaxSteepness', 'MOB_WATER_MAX_STEEPNESS'),
    ]

    failures = []
    for label, cpp_name, hlsl_name in pairs:
        cpp_value = _cpp_constant(header, cpp_name)
        hlsl_value = _hlsl_define(shader, hlsl_name)

        if cpp_value is None:
            failures.append('{0}: not found in MobWaterWaves.h'.format(label))
        elif hlsl_value is None:
            failures.append('{0}: not found in MobWaterWaves.ush'.format(label))
        elif cpp_value != hlsl_value:
            failures.append('{0}: header says {1}, shader says {2}'.format(label, cpp_value, hlsl_value))
        else:
            _log('  ok  {0} = {1}'.format(label, cpp_value))

    return failures


def check_cpd_indices():
    """The custom primitive data layout the component writes and the material reads."""
    text = _read(TYPES)

    found = {}
    for match in re.finditer(r'static\s+constexpr\s+int32\s+(\w+)\s*=\s*(\d+)\s*;', text):
        found[match.group(1)] = int(match.group(2))

    failures = []
    for name, index in sorted(EXPECTED_INDICES.items(), key=lambda item: item[1]):
        if name not in found:
            failures.append('MobWaterData::{0} is gone.'.format(name))
        elif found[name] != index:
            failures.append('MobWaterData::{0} moved from {1} to {2}, so the material and the '
                            'component no longer mean the same thing by it.'
                            .format(name, index, found[name]))
        else:
            _log('  ok  MobWaterData::{0} = {1}'.format(name, index))

    over = sorted(n for n, i in EXPECTED_INDICES.items()
                  if n != 'Num' and i >= CUSTOM_PRIMITIVE_DATA_FLOATS)
    if over:
        failures.append('{0} past the end of the engine\'s custom primitive data ({1} floats). The '
                        'component writes them, the write is discarded, and the material reads zero: '
                        '{2}'.format(len(over), CUSTOM_PRIMITIVE_DATA_FLOATS, ', '.join(over)))
    else:
        _log('  ok  {0} of {1} custom primitive data floats used'
             .format(EXPECTED_INDICES['Num'], CUSTOM_PRIMITIVE_DATA_FLOATS))

    return failures


PROBE_PATH = '/MobWater/Materials/M_MobWaterParity'
PRESET_PATH = '/MobWater/Waves/WP_MobWater_Ocean'

PROBE_SIZE = 8
PROBE_EXTENT = 4000.0
PROBE_ORIGIN = (1234.0, -5678.0)
PROBE_ENCODE_SCALE = 1000.0

# The instants to compare at. Zero, something small, and something far enough out that a phase
# folded one way and not the other has visibly parted.
PROBE_TIMES = [0.0, 3.7, 91.3]

# Centimetres. The two sides are the same arithmetic in the same precision, but not in the same
# order - a shader folds constants and reassociates freely - so they agree to about a part in a
# million of the amplitude rather than exactly. An ocean crest is 85cm; a tenth of a millimetre is
# far tighter than anything that could hide a real difference in the maths.
PROBE_TOLERANCE = 0.01


def check_wave_parity():
    """Evaluates the wave set on the GPU and on the CPU and compares them.

    This is the check the whole plugin rests on. MobWaterWaves.h and MobWaterWaves.ush are one
    algorithm written twice, and a buoyancy query that answers a different surface from the one being
    drawn is the failure this exists to catch - it looks like bad tuning rather than like a bug, and
    it is invisible until something floats through a wave.
    """
    probe = unreal.load_asset(PROBE_PATH)
    if probe is None:
        return ['parity probe %s is missing. Run Water > Generate Materials.' % PROBE_PATH]

    preset = unreal.load_asset(PRESET_PATH)
    if preset is None:
        return ['wave preset %s is missing. Run Water > Generate Materials.' % PRESET_PATH]

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return ['no editor world to render the probe in.']

    target = unreal.RenderingLibrary.create_render_target2d(
        world, PROBE_SIZE, PROBE_SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
    if target is None:
        return ['could not create a render target for the probe.']

    # Prove the target can be written and read at all before trusting anything read out of it.
    #
    # Without this, a context where rendering does not run - a commandlet on a machine with no RHI,
    # a cook, anything headless - hands back a black target, and every texel decodes to a large
    # negative number that looks exactly like the shader and the header having parted. The test would
    # then blame the maths for the absence of a renderer, which is the worst answer it could give.
    unreal.RenderingLibrary.clear_render_target2d(world, target, unreal.LinearColor(0.25, 0.5, 0.75, 1.0))
    probe_pixel = unreal.RenderingLibrary.read_render_target_raw_pixel(world, target, 0, 0)

    if abs(probe_pixel.r - 0.25) > 1e-3 or abs(probe_pixel.g - 0.5) > 1e-3:
        return ['render targets cannot be read back here (a cleared target came back as '
                '({0:.3f}, {1:.3f}, {2:.3f})). Run Water > Verify Contract from the editor, where '
                'rendering is live.'.format(probe_pixel.r, probe_pixel.g, probe_pixel.b)]

    material = unreal.MaterialLibrary.create_dynamic_material_instance(world, probe)

    waves = preset.get_editor_property('waves')
    entries = list(waves.get_editor_property('waves'))

    material.set_vector_parameter_value('WaveScales', unreal.LinearColor(
        float(len(entries)),
        waves.get_editor_property('amplitude_scale'),
        waves.get_editor_property('speed_scale'),
        waves.get_editor_property('choppiness_scale')))

    for index in range(8):
        if index < len(entries):
            wave = entries[index]
            direction = wave.get_editor_property('direction')

            # FVector2f's binding exposes no .x, only the reflected property.
            raw_x = direction.get_editor_property('x')
            raw_y = direction.get_editor_property('y')

            # Normalised here as well as in the shader, because the preset holds whatever was typed
            # and the two sides have to be handed the same numbers, not merely equivalent ones.
            length = math.sqrt(raw_x * raw_x + raw_y * raw_y)
            dx = raw_x / length if length > 1e-4 else 0.0
            dy = raw_y / length if length > 1e-4 else 0.0

            material.set_vector_parameter_value('WaveA%d' % index, unreal.LinearColor(
                dx, dy, wave.get_editor_property('wavelength'), wave.get_editor_property('amplitude')))
            material.set_vector_parameter_value('WaveB%d' % index, unreal.LinearColor(
                wave.get_editor_property('steepness'), wave.get_editor_property('phase_offset'), 0.0, 0.0))
        else:
            material.set_vector_parameter_value('WaveA%d' % index, unreal.LinearColor(0.0, 0.0, 100.0, 0.0))
            material.set_vector_parameter_value('WaveB%d' % index, unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    material.set_vector_parameter_value('ProbeOrigin', unreal.LinearColor(PROBE_ORIGIN[0], PROBE_ORIGIN[1], 0.0, 0.0))
    material.set_scalar_parameter_value('ProbeExtent', PROBE_EXTENT)
    material.set_scalar_parameter_value('EncodeScale', PROBE_ENCODE_SCALE)

    failures = []
    worst = 0.0
    compared = 0
    moved = False
    drew = False

    for time in PROBE_TIMES:
        material.set_scalar_parameter_value('Time', time)

        unreal.RenderingLibrary.draw_material_to_render_target(world, target, material)

        for ty in range(PROBE_SIZE):
            for tx in range(PROBE_SIZE):
                pixel = unreal.RenderingLibrary.read_render_target_raw_pixel(world, target, tx, ty)

                # An untouched target is exactly zero, which is not a value the probe can write:
                # everything it writes is biased into the positive half around 0.5.
                if pixel.r != 0.0 or pixel.g != 0.0 or pixel.b != 0.0:
                    drew = True

                gpu = unreal.Vector(
                    (pixel.r - 0.5) * PROBE_ENCODE_SCALE,
                    (pixel.g - 0.5) * PROBE_ENCODE_SCALE,
                    (pixel.b - 0.5) * PROBE_ENCODE_SCALE)

                # The same place the shader thinks this texel is: texel centres, not corners.
                u = (tx + 0.5) / PROBE_SIZE
                v = (ty + 0.5) / PROBE_SIZE

                sample = unreal.Vector2D(
                    PROBE_ORIGIN[0] + (u - 0.5) * PROBE_EXTENT,
                    PROBE_ORIGIN[1] + (v - 0.5) * PROBE_EXTENT)

                cpu, _normal, _fold = unreal.MobWaterStatics.evaluate_wave_preset(preset, sample, time)

                delta = max(abs(cpu.x - gpu.x), abs(cpu.y - gpu.y), abs(cpu.z - gpu.z))
                worst = max(worst, delta)
                compared += 1

                if abs(cpu.z) > 1e-3:
                    moved = True

                if not drew:
                    continue

                if delta > PROBE_TOLERANCE and len(failures) < 4:
                    failures.append(
                        'wave parity at ({0:.0f}, {1:.0f}) t={2}: CPU ({3:.4f}, {4:.4f}, {5:.4f}) '
                        'GPU ({6:.4f}, {7:.4f}, {8:.4f}), off by {9:.4f}cm. MobWaterWaves.h and '
                        'MobWaterWaves.ush have parted.'
                        .format(sample.x, sample.y, time, cpu.x, cpu.y, cpu.z, gpu.x, gpu.y, gpu.z, delta))

    if not drew:
        return ['the probe material drew nothing - every texel came back as an untouched zero, '
                'though the target itself reads back fine. That means the material failed to compile '
                'and the engine substituted the default one, which draws black. Search the log for '
                '"Failed to compile Material /MobWater/Materials/M_MobWaterParity"; the real error is '
                'the tab-indented line after it.']

    # A comparison of two zeroes agrees perfectly and proves nothing. This is the check that the
    # check was actually exercised.
    if not moved:
        failures.append('the wave set never displaced anything, so parity was compared against '
                        'nothing. Check that %s has waves in it.' % PRESET_PATH)

    if not failures:
        _log('  ok  {0} points across {1} instants, worst disagreement {2:.5f}cm'
             .format(compared, len(PROBE_TIMES), worst))

    return failures


def run():
    """Every check. Returns True when they all pass."""
    _log('Verifying contract')

    failures = []

    _log(' constants shared by MobWaterWaves.h and MobWaterWaves.ush')
    failures += check_constants()

    _log(' custom primitive data indices')
    failures += check_cpd_indices()

    _log(' numeric CPU and GPU wave parity')
    if unreal:
        failures += check_wave_parity()
    else:
        _log('  --  skipped: needs the editor to render the probe.')

    importlib.reload(mob_water_version)
    _log(' generated content is version %s' % mob_water_version.plugin_version())
    if unreal:
        failures += mob_water_version.check()
    else:
        _log('  --  skipped: needs the editor to load the assets.')

    if failures:
        for failure in failures:
            _log_error(failure)
        _log_error('{0} check(s) failed.'.format(len(failures)))
        return False

    _log('All checks passed.')
    return True


if __name__ == '__main__':
    run()
