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

SPECTRUM_HEADER = os.path.join(PLUGIN_ROOT, 'Source', 'MobWater', 'Public', 'MobWaterSpectrum.h')
SPECTRUM_SHADER = os.path.join(PLUGIN_ROOT, 'Shaders', 'Public', 'MobWaterSpectrum.ush')

UNDERWATER = os.path.join(PLUGIN_ROOT, 'Source', 'MobWater', 'Private', 'MobWaterUnderwaterComponent.cpp')
GENERATOR = os.path.join(PLUGIN_ROOT, 'Python', 'author_water.py')

# The underwater plane's own layout, which is small and has nothing to do with a body of water's.
# Named here rather than derived, so a rename on either side fails by name instead of by silence.
EXPECTED_UNDERWATER = {
    'AbsorbColor': 'UW_ABSORB_COLOR',
    'Clarity': 'UW_CLARITY',
    'Submersion': 'UW_SUBMERSION',
    'SurfaceNormal': 'UW_SURFACE_NORMAL',
    'ImmersionDepth': 'UW_IMMERSION_DEPTH',
    'MeniscusThickness': 'UW_MENISCUS_THICKNESS',
    'MeniscusStrength': 'UW_MENISCUS_STRENGTH',
    'CausticStrength': 'UW_CAUSTIC_STRENGTH',
    'CausticScale': 'UW_CAUSTIC_SCALE',
    'CausticDepth': 'UW_CAUSTIC_DEPTH',
}


# The custom primitive data layout, as the README and the generator both understand it.
#
# This table is the contract and the header is checked against it rather than the other way round -
# a typo in the header should fail, not redefine the contract.
EXPECTED_INDICES = {
    'ShallowColor': 0,
    # The same float. Only one of the two forks is ever compiled, so the colours' six floats are free
    # whenever the row is wanted.
    'GradientRow': 0,
    'DeepColor': 3,
    'FadeDepth': 6,
    'ClarityDepth': 7,
    'ShoreFoamDepth': 8,
    'CrestFoamThreshold': 9,
    'WaveAmplitude': 10,
    # The same float again. The amplitude is the whole part in hundredths and the speed is the
    # fraction, which the primitive data being full at thirty six is the reason for.
    'WaveSpeed': 10,
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


def _is_commandlet():
    """Whether this is a commandlet rather than the editor.

    Texture streaming does not run in one, so a probe that samples a texture asset reads whatever the
    smallest resident mip holds. That is worth telling apart from the same symptom in the editor,
    where it would mean something genuinely broken.
    """
    try:
        return '-run=' in unreal.SystemLibrary.get_command_line().lower()
    except AttributeError:
        return False


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

    spectrum_header = _read(SPECTRUM_HEADER)
    spectrum_shader = _read(SPECTRUM_SHADER)

    pairs = [
        ('Gravity', 'Gravity', 'MOB_WATER_GRAVITY', header, shader),
        ('MaxWaves', 'MaxWaves', 'MOB_WATER_MAX_WAVES', header, shader),
        ('MaxSteepness', 'MaxSteepness', 'MOB_WATER_MAX_STEEPNESS', header, shader),
        # What a body's packed wave fraction is measured in. Off, the amplitude still reads right
        # and every body runs at a speed nobody set, which reads as the wave preset being wrong.
        ('SpeedRange', 'SpeedRange', 'MOB_WATER_SPEED_RANGE', header, shader),
        # Off by one and every tile boundary in the ocean carries a band of water from the wrong
        # frame, which is one texel wide and therefore looks like a compression artefact.
        ('Gutter', 'Gutter', 'MOB_WATER_SPECTRUM_GUTTER', spectrum_header, spectrum_shader),
    ]

    failures = []
    for label, cpp_name, hlsl_name, cpp_text, hlsl_text in pairs:
        cpp_value = _cpp_constant(cpp_text, cpp_name)
        hlsl_value = _hlsl_define(hlsl_text, hlsl_name)

        if cpp_value is None:
            failures.append('{0}: not found in the header'.format(label))
        elif hlsl_value is None:
            failures.append('{0}: not found in the shader'.format(label))
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


def check_underwater_indices():
    """The plane's data layout, as the component writes it and the generator reads it.

    Its own layout rather than MobWaterData, because the plane is not a body of water and shares
    almost nothing with one - and because it is the one place a wrong index shows as nothing at all:
    an underwater view that never comes on looks exactly like a camera that never went under.
    """
    component = _read(UNDERWATER)
    generator = _read(GENERATOR)

    failures = []
    for name, constant in sorted(EXPECTED_UNDERWATER.items(), key=lambda pair: pair[0]):
        cpp = _cpp_constant(component, name)

        match = re.search(r'^' + constant + r'\s*=\s*(\d+)\s*$', generator, re.MULTILINE)
        python = int(match.group(1)) if match else None

        if cpp is None:
            failures.append('MobUnderwaterData::{0} is gone.'.format(name))
        elif python is None:
            failures.append('{0} is gone from author_water.'.format(constant))
        elif int(cpp) != python:
            failures.append('MobUnderwaterData::{0} is {1} and {2} is {3}, so the plane writes one '
                            'thing and reads another.'.format(name, int(cpp), constant, python))
        else:
            _log('  ok  MobUnderwaterData::{0} = {1}'.format(name, int(cpp)))

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

# The body scales to run the whole comparison at, as (amplitude, speed).
#
# One pair for a body that set neither, and one that sets both to something no quantisation lands on
# by luck. The two share a data slot, so the second pair is the only thing that tells a packing read
# the wrong way round from one read the right way - the waves either side of it are correct in both
# cases, and every body simply runs at a height and a speed nobody typed.
PROBE_BODY_SCALES = [(1.0, 1.0), (0.37, 0.62)]


def _scaled_preset(preset, amplitude, speed):
    """The same wave set with a body's own scales already in it.

    Built fresh rather than by editing the asset's copy, because a struct read out of an asset can
    write back through and a verify pass has no business dirtying the preset it is checking.
    """
    source = preset.get_editor_property('waves')

    params = unreal.MobWaterWaveParams()
    params.set_editor_property('waves', list(source.get_editor_property('waves')))
    params.set_editor_property('amplitude_scale',
                              source.get_editor_property('amplitude_scale') * amplitude)
    params.set_editor_property('speed_scale', source.get_editor_property('speed_scale') * speed)
    params.set_editor_property('choppiness_scale', source.get_editor_property('choppiness_scale'))

    scaled = unreal.new_object(unreal.MobWaterWavePreset)
    scaled.set_editor_property('waves', params)

    return scaled


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

    for wanted_amplitude, wanted_speed in PROBE_BODY_SCALES:
        # Through the pack and back out, because that is all the shader ever sees. Comparing against
        # what was asked for would fail on the quantisation rather than on the maths.
        packed = unreal.MobWaterStatics.pack_body_wave_scales(wanted_amplitude, wanted_speed)
        amplitude, speed = unreal.MobWaterStatics.unpack_body_wave_scales(packed)

        material.set_scalar_parameter_value('BodyScales', packed)
        scaled = _scaled_preset(preset, amplitude, speed)

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

                    cpu, _normal, _fold = unreal.MobWaterStatics.evaluate_wave_preset(
                        scaled, sample, time)

                    delta = max(abs(cpu.x - gpu.x), abs(cpu.y - gpu.y), abs(cpu.z - gpu.z))
                    worst = max(worst, delta)
                    compared += 1

                    if abs(cpu.z) > 1e-3:
                        moved = True

                    if not drew:
                        continue

                    if delta > PROBE_TOLERANCE and len(failures) < 4:
                        failures.append(
                            'wave parity at ({0:.0f}, {1:.0f}) t={2} at amplitude {3} speed {4}: '
                            'CPU ({5:.4f}, {6:.4f}, {7:.4f}) GPU ({8:.4f}, {9:.4f}, {10:.4f}), off '
                            'by {11:.4f}cm. MobWaterWaves.h and MobWaterWaves.ush have parted.'
                            .format(sample.x, sample.y, time, amplitude, speed,
                                    cpu.x, cpu.y, cpu.z, gpu.x, gpu.y, gpu.z, delta))

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
        _log('  ok  {0} points across {1} instants and {2} body scales, worst disagreement '
             '{3:.5f}cm'.format(compared, len(PROBE_TIMES), len(PROBE_BODY_SCALES), worst))

    return failures


SPECTRUM_PROBE_PATH = '/MobWater/Materials/M_MobWaterSpectrumParity'
SPECTRUM_ASSET_PATH = '/MobWater/Spectra/SP_MobWater_Ocean'

# Wider than the wave probe's, and deliberately: it has to cross several tiles so a tile boundary and
# the gutter that stands in for wrapping there are both inside the sample set.
SPECTRUM_EXTENT = 20000.0

# Instants that land between frames as well as on them. A blend weight of zero would compare the two
# implementations at the one moment where neither is interpolating.
SPECTRUM_TIMES = [0.0, 2.34, 11.71]

# Centimetres.
#
# Looser than the wave probe's, and that is the honest number rather than a concession. Both sides
# read the same bytes, so nothing here is a difference of opinion about the sea - what is left is
# that the GPU's bilinear weights are fixed point, and eight bits of subtexel precision across a
# texel most of a metre wide is half a millimetre whatever either side does. The check prints what it
# actually measured, and that figure is the one the docs quote.
#
# It found a real fault at ten times this, and the fault was the sampler rather than the maths: the
# shared wrap sampler is anisotropic, and an anisotropic read of a frame atlas averages in the
# neighbouring frame. The taps name their mip level now, which is what makes a widening filter
# impossible. Anything that is not rounding lands well outside this - an atlas addressed at the wrong
# scale, a frame index off by one, a decode that lost its bias, a gutter that is not there.
SPECTRUM_TOLERANCE = 0.2


def check_spectrum_parity():
    """Reads the baked sea on the GPU and in the query, and compares them."""
    spectrum = unreal.load_asset(SPECTRUM_ASSET_PATH)
    if spectrum is None:
        return ['%s is missing. Run Water > Bake Ocean Spectrum.' % SPECTRUM_ASSET_PATH]

    if spectrum.get_table_bytes() == 0:
        return ['%s has no query table, so a dedicated server would answer a flat sea while the '
                'clients drew waves. Run Water > Bake Ocean Spectrum.' % SPECTRUM_ASSET_PATH]

    probe = unreal.load_asset(SPECTRUM_PROBE_PATH)
    if probe is None:
        return ['spectrum probe %s is missing. Run Water > Generate Materials.' % SPECTRUM_PROBE_PATH]

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return ['no editor world to render the spectrum probe in.']

    target = unreal.RenderingLibrary.create_render_target2d(
        world, PROBE_SIZE, PROBE_SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
    if target is None:
        return ['could not create a render target for the spectrum probe.']

    material = unreal.MaterialLibrary.create_dynamic_material_instance(world, probe)

    resolution = spectrum.get_editor_property('resolution')
    frames = spectrum.get_editor_property('frames')

    material.set_vector_parameter_value('SpectrumParams', unreal.LinearColor(
        spectrum.get_editor_property('tile_size'),
        spectrum.get_editor_property('loop_period'),
        float(resolution),
        float(frames)))

    material.set_vector_parameter_value('SpectrumScale', unreal.LinearColor(
        spectrum.get_editor_property('horizontal_scale'),
        spectrum.get_editor_property('vertical_scale'),
        spectrum.get_editor_property('normal_scale'),
        float(spectrum.get_editor_property('atlas_columns'))))

    material.set_texture_parameter_value('SpectrumDisplacement',
                                         spectrum.get_editor_property('displacement_texture'))

    material.set_vector_parameter_value('ProbeOrigin', unreal.LinearColor(
        PROBE_ORIGIN[0], PROBE_ORIGIN[1], 0.0, 0.0))
    material.set_scalar_parameter_value('ProbeExtent', SPECTRUM_EXTENT)
    material.set_scalar_parameter_value('EncodeScale', PROBE_ENCODE_SCALE)

    failures = []
    worst = 0.0
    compared = 0
    moved = False
    drew = False
    flat = True
    first = None

    for time in SPECTRUM_TIMES:
        material.set_scalar_parameter_value('Time', time)

        unreal.RenderingLibrary.draw_material_to_render_target(world, target, material)

        for ty in range(PROBE_SIZE):
            for tx in range(PROBE_SIZE):
                pixel = unreal.RenderingLibrary.read_render_target_raw_pixel(world, target, tx, ty)

                if pixel.r != 0.0 or pixel.g != 0.0 or pixel.b != 0.0:
                    drew = True

                gpu = unreal.Vector(
                    (pixel.r - 0.5) * PROBE_ENCODE_SCALE,
                    (pixel.g - 0.5) * PROBE_ENCODE_SCALE,
                    (pixel.b - 0.5) * PROBE_ENCODE_SCALE)

                if first is None:
                    first = gpu
                elif abs(gpu.z - first.z) > 1e-4:
                    flat = False

                u = (tx + 0.5) / PROBE_SIZE
                v = (ty + 0.5) / PROBE_SIZE

                sample = unreal.Vector2D(
                    PROBE_ORIGIN[0] + (u - 0.5) * SPECTRUM_EXTENT,
                    PROBE_ORIGIN[1] + (v - 0.5) * SPECTRUM_EXTENT)

                cpu, _fold = unreal.MobWaterStatics.evaluate_spectrum(spectrum, sample, time)

                delta = max(abs(cpu.x - gpu.x), abs(cpu.y - gpu.y), abs(cpu.z - gpu.z))
                worst = max(worst, delta)
                compared += 1

                if abs(cpu.z) > 1e-3:
                    moved = True

                if not drew:
                    continue

                if delta > SPECTRUM_TOLERANCE and len(failures) < 4:
                    failures.append(
                        'spectrum parity at ({0:.0f}, {1:.0f}) t={2}: CPU ({3:.4f}, {4:.4f}, {5:.4f}) '
                        'GPU ({6:.4f}, {7:.4f}, {8:.4f}), off by {9:.4f}cm. The atlas and the table '
                        'are not being read as the same field.'
                        .format(sample.x, sample.y, time, cpu.x, cpu.y, cpu.z,
                                gpu.x, gpu.y, gpu.z, delta))

    if not drew:
        return ['the spectrum probe drew nothing. Search the log for "Failed to compile Material '
                '%s"; the real error is the tab-indented line after it.' % SPECTRUM_PROBE_PATH]

    # One value everywhere, over twenty thousand centimetres of open sea, is not a sea - it is the
    # atlas not having been sampled at all. That is what a commandlet does: nothing streams a texture
    # in, so the tap reads whatever the smallest resident mip holds and every texel gets the same
    # answer. Reported as a skip rather than a failure, because the maths it would be blaming is not
    # what went wrong, and reported at all rather than silently passed.
    if flat:
        if _is_commandlet():
            _log('  --  skipped: the baked atlas is not resident in a commandlet, so every texel of '
                 'the probe came back the same. Run Water > Verify Contract from the editor.')
            return []

        return ['every texel of the spectrum probe came back the same value, over two hundred metres '
                'of open sea. The atlas is not being sampled at all - either the texture parameter '
                'is unbound or what is bound has no mip resident.']

    if not moved:
        failures.append('the baked sea never displaced anything, so parity was compared against '
                        'nothing. Check that %s has a table in it.' % SPECTRUM_ASSET_PATH)

    if not failures:
        _log('  ok  {0} points across {1} instants, worst disagreement {2:.5f}cm'
             .format(compared, len(SPECTRUM_TIMES), worst))
        _log('      (a filter weight, not a difference of maths - both sides read the same bytes)')

    return failures


EXCLUSION_PROBE_PATH = '/MobWater/Materials/M_MobWaterExclusionParity'
EXCLUSION_MESH_PATH = '/MobWater/Meshes/SM_MobWaterDisc'

EXCLUSION_PROBE_SIZE = 12

# A whole number of outline texels from the origin, so the window's own grid snap moves it nowhere and
# the probe and the window are centred on the same point rather than within a texel of each other.
# 2000cm over 256 texels is 7.8125, and both of these divide by it exactly.
EXCLUSION_ORIGIN = (12000.0, -8000.0)

# Well inside the outline window, which is 2000cm across and fades over its outer twentieth.
EXCLUSION_EXTENT = 800.0

# How much water, out of all of it.
#
# The analytic route is float arithmetic on both sides and agrees to rounding. The outline route
# cannot: what the query samples directly, the surface reads back out of an eight bit target, so a
# value halfway between two of its 255 steps is half a step out however right both sides are. That
# floor is 0.002, and this is a little over it. Both figures are printed rather than assumed.
EXCLUSION_TOLERANCE = 0.004
EXCLUSION_MESH_TOLERANCE = 0.01

# Half the mesh volume's footprint, and how soft its edge is. The softness is generous deliberately:
# an edge sharper than the window's texels is an edge the window cannot carry, and a check that
# demanded one would be measuring the target's resolution rather than the plugin's arithmetic.
#
# The generated meshes are a unit across, because a body of water carries its size in its own scale
# rather than in its geometry - so a disc three metres wide is a scale of six hundred, not of three.
EXCLUSION_MESH_SCALE = 600.0
EXCLUSION_MESH_SOFTNESS = 150.0


def _spawn_exclusion(world, shape, offset, extent, softness, strength=1.0, yaw=0.0, scale=1.0):
    """One volume, placed relative to the probe's origin."""
    location = unreal.Vector(EXCLUSION_ORIGIN[0] + offset[0], EXCLUSION_ORIGIN[1] + offset[1], 0.0)

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.MobWaterExclusion, location, unreal.Rotator(0.0, 0.0, yaw))
    if actor is None:
        return None

    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))

    component = actor.get_editor_property('exclusion')
    component.set_editor_property('shape', shape)
    component.set_editor_property('strength', strength)
    component.set_editor_property('edge_softness', softness)
    component.set_editor_property('blocks_submersion', True)

    if shape == unreal.MobWaterExclusionShape.MESH:
        component.set_editor_property('exclusion_mesh', unreal.load_asset(EXCLUSION_MESH_PATH))
        component.rebuild_silhouette()
    else:
        component.set_editor_property('extent', unreal.Vector2D(extent[0], extent[1]))

    return actor


def _compare_exclusion(world, material, target, tolerance, label):
    """Draws the probe and compares every texel against what the query answers there.

    Returns (failures, worst, compared, most), where most is the largest exclusion any sampled point
    actually had. A comparison over water nothing was keeping out agrees perfectly and proves nothing,
    so the caller checks that figure before believing the rest.
    """
    unreal.RenderingLibrary.draw_material_to_render_target(world, target, material)

    failures = []
    worst = 0.0
    compared = 0
    most = 0.0

    for ty in range(EXCLUSION_PROBE_SIZE):
        for tx in range(EXCLUSION_PROBE_SIZE):
            pixel = unreal.RenderingLibrary.read_render_target_raw_pixel(world, target, tx, ty)

            u = (tx + 0.5) / EXCLUSION_PROBE_SIZE
            v = (ty + 0.5) / EXCLUSION_PROBE_SIZE

            location = unreal.Vector(
                EXCLUSION_ORIGIN[0] + (u - 0.5) * EXCLUSION_EXTENT,
                EXCLUSION_ORIGIN[1] + (v - 0.5) * EXCLUSION_EXTENT,
                0.0)

            cpu = unreal.MobWaterSubsystem.get_exclusion_at_location(world, location)
            gpu = pixel.b

            delta = abs(cpu - gpu)
            worst = max(worst, delta)
            most = max(most, cpu)
            compared += 1

            if delta > tolerance and len(failures) < 4:
                failures.append(
                    '{0} exclusion at ({1:.0f}, {2:.0f}): the query says {3:.4f} and the surface '
                    'draws {4:.4f} (volumes {5:.4f}, outlines {6:.4f}), off by {7:.4f}. What is '
                    'carved and what is answered are not the same shape.'
                    .format(label, location.x, location.y, cpu, gpu, pixel.r, pixel.g, delta))

    return failures, worst, compared, most


def check_exclusion_parity():
    """Reads back what the surface carves and compares it against what the query answers.

    Both wave probes compare one piece of arithmetic written twice. This one does not: it places real
    volumes, lets the subsystem publish them exactly as it would on any frame, and asks the two sides
    the same question. What it catches is a break anywhere along that path - a slot packed the wrong
    way round, an outline drawn into the wrong window, a rotation applied on one side only - none of
    which any amount of reading the two implementations would show, because they are not two
    implementations of anything.

    Only volumes that block submersion are comparable, so the ones it places all do. The query counts
    every volume there is and the surface only the nearest few, so it stays well under both budgets.
    """
    probe = unreal.load_asset(EXCLUSION_PROBE_PATH)
    if probe is None:
        return ['exclusion probe %s is missing. Run Water > Generate Materials.' % EXCLUSION_PROBE_PATH]

    if unreal.load_asset(EXCLUSION_MESH_PATH) is None:
        return ['%s is missing, so there is nothing to cut an outline from. Run Water > Generate '
                'Materials.' % EXCLUSION_MESH_PATH]

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        return ['no editor world to place exclusion volumes in.']

    target = unreal.RenderingLibrary.create_render_target2d(
        world, EXCLUSION_PROBE_SIZE, EXCLUSION_PROBE_SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA32F)
    if target is None:
        return ['could not create a render target for the exclusion probe.']

    material = unreal.MaterialLibrary.create_dynamic_material_instance(world, probe)
    material.set_vector_parameter_value(
        'ProbeOrigin', unreal.LinearColor(EXCLUSION_ORIGIN[0], EXCLUSION_ORIGIN[1], 0.0, 0.0))
    material.set_scalar_parameter_value('ProbeExtent', EXCLUSION_EXTENT)

    centre = unreal.Vector(EXCLUSION_ORIGIN[0], EXCLUSION_ORIGIN[1], 0.0)

    failures = []
    spawned = []

    try:
        # The analytic volumes on their own first. Run together with the outline the two routes are
        # combined by a max, and either one being right would hide the other being wrong wherever
        # they overlap.
        spawned.append(_spawn_exclusion(
            world, unreal.MobWaterExclusionShape.DISC, (-220.0, -180.0), (180.0, 180.0), 60.0))
        spawned.append(_spawn_exclusion(
            world, unreal.MobWaterExclusionShape.BOX, (240.0, 200.0), (150.0, 90.0), 40.0, yaw=31.0))
        spawned.append(_spawn_exclusion(
            world, unreal.MobWaterExclusionShape.RECT, (-260.0, 260.0), (120.0, 120.0), 50.0,
            strength=0.45))

        if None in spawned:
            return ['could not place an exclusion volume in the editor world.']

        unreal.MobWaterSubsystem.refresh_exclusions(world, centre)

        volume_failures, volume_worst, volume_points, volume_most = _compare_exclusion(
            world, material, target, EXCLUSION_TOLERANCE, 'analytic')

        failures += volume_failures

        if volume_most < 0.5:
            failures.append('the analytic volumes kept no water out anywhere the probe looked, so '
                            'exclusion parity was compared against nothing.')
        elif not volume_failures:
            _log('  ok  analytic: {0} points, worst disagreement {1:.5f}'
                 .format(volume_points, volume_worst))

        for actor in spawned:
            unreal.EditorLevelLibrary.destroy_actor(actor)
        spawned = []

        # And now the outline, alone, which is the route that used to be a bounding rectangle.
        mesh_volume = _spawn_exclusion(
            world, unreal.MobWaterExclusionShape.MESH, (0.0, 0.0), (0.0, 0.0),
            EXCLUSION_MESH_SOFTNESS, scale=EXCLUSION_MESH_SCALE)

        if mesh_volume is None:
            return ['could not place a mesh exclusion volume in the editor world.']

        spawned.append(mesh_volume)

        component = mesh_volume.get_editor_property('exclusion')
        if not component.is_mesh():
            failures.append('%s produced no outline, so the mesh route was never exercised - the '
                            'mask is baked from the mesh description, which a mesh still building '
                            'does not have yet.' % EXCLUSION_MESH_PATH)
        else:
            unreal.MobWaterSubsystem.refresh_exclusions(world, centre)

            mesh_failures, mesh_worst, mesh_points, mesh_most = _compare_exclusion(
                world, material, target, EXCLUSION_MESH_TOLERANCE, 'outline')

            failures += mesh_failures

            if mesh_most < 0.5:
                failures.append('the baked outline kept no water out anywhere the probe looked. '
                                'Either it was not drawn into the window or the window is not where '
                                'the probe is.')
            elif not mesh_failures:
                _log('  ok  outline: {0} points, worst disagreement {1:.5f}'
                     .format(mesh_points, mesh_worst))

            # The outline is a disc, so its bounding rectangle's corners are outside it. This is the
            # one assertion that says the mesh route is the mesh rather than the box around it, and
            # it is worth stating separately: a bounding rectangle agrees with itself perfectly on
            # both sides, so every comparison above would pass while the shape was wrong.
            reach = component.get_world_extent()
            corner = unreal.Vector(
                EXCLUSION_ORIGIN[0] + reach.x * 0.92,
                EXCLUSION_ORIGIN[1] + reach.y * 0.92,
                0.0)

            at_corner = unreal.MobWaterSubsystem.get_exclusion_at_location(world, corner)
            if at_corner > 0.01:
                failures.append('the outline excludes {0:.3f} at the corner of its own bounding '
                                'rectangle, where the mesh is not. It is behaving as its bounds '
                                'rather than as its shape.'.format(at_corner))
            else:
                _log('  ok  outline is the shape and not its bounds ({0:.0f}, {1:.0f} is clear)'
                     .format(reach.x, reach.y))
    finally:
        for actor in spawned:
            if actor:
                unreal.EditorLevelLibrary.destroy_actor(actor)

        # Left published, the window keeps whatever the check put in it and every body of water in
        # the level has a hole cut in it by a volume that no longer exists.
        unreal.MobWaterSubsystem.refresh_exclusions(world, centre)

    return failures


def run():
    """Every check. Returns True when they all pass."""
    _log('Verifying contract')

    failures = []

    _log(' constants shared by MobWaterWaves.h and MobWaterWaves.ush')
    failures += check_constants()

    _log(' custom primitive data indices')
    failures += check_cpd_indices()

    _log(' the underwater plane-s own data layout')
    failures += check_underwater_indices()

    _log(' numeric CPU and GPU wave parity')
    if unreal:
        failures += check_wave_parity()
    else:
        _log('  --  skipped: needs the editor to render the probe.')

    _log(' numeric CPU and GPU parity on the baked sea')
    if unreal:
        failures += check_spectrum_parity()
    else:
        _log('  --  skipped: needs the editor to render the probe.')

    _log(' what the query answers against what the surface carves')
    if unreal:
        failures += check_exclusion_parity()
    else:
        _log('  --  skipped: needs the editor to place volumes and render the probe.')

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
