# Copyright (c) Jared Taylor. All Rights Reserved

"""Authors the water master material, its instances, and everything they share.

Run from the editor's Python console, or from the Water menu in the level editor toolbar:

    import sys, importlib
    sys.path.append('<PluginDir>/Python')
    import author_water
    importlib.reload(author_water)
    author_water.build_all()

Every phase is idempotent: an existing asset is emptied and rebuilt in place so placed bodies of
water keep their references. The collection is the exception - its parameters carry GUIDs that
materials reference, so it is only ever added to and pruned.

The maths lives in Shaders/Public/MobWaterWaves.ush, reached through Custom nodes. The wave set
arrives through the collection rather than as per-instance data because every body of water in a
level is in the same sea: one write moves all of them, and the alternative is a dynamic material
instance per body, which is the cost this plugin is arranged to avoid.
"""

import importlib

import unreal

import author_ripples
import mob_water_version
import mob_water_graph as g
import mob_water_spectrum
import mob_water_textures

COLLECTION_NAME = 'MPC_MobWater'

# The names the subsystem writes. MobWaterSubsystem.cpp holds the same strings, and a rename that
# reaches one side and not the other produces water that renders perfectly flat with nothing in any
# log to say why.
MAX_WAVES = 8

COLLECTION_VECTORS = (
    # Wave count, amplitude scale, speed scale, choppiness scale. Count first because it is the one
    # the shader branches on.
    [('WaveScales', (0.0, 1.0, 1.0, 1.0))]
    # (Direction.x, Direction.y, Wavelength, Amplitude). An unused slot carries a legal wavelength
    # rather than zero, because the wave number divides by it before the amplitude zeroes anything.
    + [('WaveA%d' % i, (0.0, 0.0, 100.0, 0.0)) for i in range(MAX_WAVES)]
    # (Steepness, PhaseOffset, unused, unused).
    + [('WaveB%d' % i, (0.0, 0.0, 0.0, 0.0)) for i in range(MAX_WAVES)]
)

COLLECTION_SCALARS = [
    ('Time', 0.0),
]

# Where the ripple field is: (OriginX, OriginY, Extent, 1 / Extent). Written by the subsystem every
# frame the field moves, and read by every body of water that has ripples on.
COLLECTION_VECTORS = COLLECTION_VECTORS + [('RippleArea', (0.0, 0.0, 4000.0, 0.00025))]

# Four volumes water is kept out of, published nearest the view first. Has to agree with
# MOB_WATER_EXCLUSION_SLOTS in MobWaterExclusionComponent.h and MOB_WATER_EXCLUSIONS in the shader.
EXCLUSION_SLOTS = 4

COLLECTION_VECTORS = COLLECTION_VECTORS + (
    [('ExclusionA%d' % i, (0.0, 0.0, 1.0, 1.0)) for i in range(EXCLUSION_SLOTS)]
    + [('ExclusionB%d' % i, (0.0, 0.0, 0.0, 0.0)) for i in range(EXCLUSION_SLOTS)]
    + [('ExclusionSoftness', (1.0, 1.0, 1.0, 1.0))]
    # Where the level's sun is. Tracked by the subsystem, because a lit translucent surface on this
    # renderer has nothing else to reflect.
    + [('SunDirection', (0.0, 0.0, -1.0, 0.0)), ('SunColor', (1.0, 0.95, 0.85, 1.0))]
    # (Intensity, Rotation in turns, unused, unused). The sky the water reflects.
    + [('ReflectionParams', (1.0, 0.0, 0.0, 0.0))]
    # How the baked sea state is laid out: (TileSize, LoopPeriod, Resolution, Frames) and
    # (HorizontalScale, VerticalScale, NormalScale, AtlasColumns). Written by the subsystem when an
    # ocean says which spectrum it is on. The scales default to zero, so a level with no ocean in it
    # publishes nothing and the atlas contributes no displacement rather than a full swing of one.
    + [('SpectrumParams', (1024.0, 1.0, 4.0, 2.0)), ('SpectrumScale', (0.0, 0.0, 0.0, 1.0))]
)


def build_parameter_collection():
    """The collection every water material reads its waves and its clock out of."""
    return g.ensure_collection(g.MAT_ROOT, COLLECTION_NAME, COLLECTION_VECTORS, COLLECTION_SCALARS)


# ---------------------------------------------------------------------------
# The colour ramps
# ---------------------------------------------------------------------------

GRAD_ROOT = g.ROOT + '/Gradients'
GRADIENT_ASSET = GRAD_ROOT + '/GA_MobWater'

# What the Gradient asset bakes itself into. Exposed as a texture parameter, so a project points an
# instance at an atlas of its own rather than editing the one that ships.
GRADIENT_TEXTURE = GRAD_ROOT + '/T_GA_MobWater'

GI = unreal.GradientInterp
GB = unreal.GradientBlendSpace

# Row order is what GradientRow indexes, and a material instance holds the number rather than the
# name, so reordering these repaints every body of water that was set to one of the later rows.
#
# Each row is the water read downwards: 0 is the surface over nothing, 1 is the surface over as much
# water as Fade Depth says a body holds. That is the same coordinate the absorption fork grades
# along, which is what lets a body switch between them without retuning its depths.
#
# OkLab, because a ramp from turquoise to deep blue passes through the midpoint where linear RGB goes
# grey - and a grey band across the middle of a pool is the one artefact nobody would author.
GRADIENT_ROWS = [
    # The palette WL_MobWater_Stylized grades to by absorption, as a ramp. Switching a stylized body
    # to the gradient should change how the colour steps, not which colours it steps between.
    ('Stylized', GB.OK_LAB, [
        (0.00, (0.20, 0.85, 0.80), GI.LINEAR),
        (0.45, (0.09, 0.62, 0.79), GI.EASE),
        (1.00, (0.02, 0.35, 0.70), GI.EASE),
    ]),
    # Three colours and nothing between them. Constant holds the previous stop and jumps at this one,
    # so the band edges land exactly where the stops are rather than where a blend happens to cross.
    ('Toon', GB.OK_LAB, [
        (0.00, (0.11, 0.45, 0.74), GI.LINEAR),
        (0.34, (0.06, 0.26, 0.55), GI.CONSTANT),
        (0.67, (0.03, 0.14, 0.38), GI.CONSTANT),
        (1.00, (0.02, 0.09, 0.26), GI.CONSTANT),
    ]),
    # Shallow tropical water: sand showing through the first hand's depth, then the drop-off.
    ('Tropical', GB.OK_LAB, [
        (0.00, (0.62, 0.80, 0.72), GI.LINEAR),
        (0.20, (0.24, 0.78, 0.76), GI.EASE),
        (0.60, (0.03, 0.42, 0.66), GI.EASE),
        (1.00, (0.01, 0.12, 0.30), GI.EASE),
    ]),
    # Standing water over rotting ground. Nearly opaque by the second stop, which is the read: a
    # swamp is not clear water that happens to be green.
    ('Swamp', GB.OK_LAB, [
        (0.00, (0.28, 0.32, 0.16), GI.LINEAR),
        (0.30, (0.13, 0.18, 0.09), GI.EASE),
        (1.00, (0.03, 0.05, 0.03), GI.EASE),
    ]),
]


def build_gradients():
    """GA_MobWater, the palettes the gradient fork reads.

    Held by GradientTool rather than baked here, because a palette that cannot be edited without
    re-running a generator is not a palette. The asset bakes its own texture on every edit, so a
    project can open it, drag a stop, and see the water change.

    Rebuilt in place like everything else here, so a palette edited in this asset is a palette the
    next generate overwrites. A project's own colours belong in a copy of it, with the instance's
    Color Gradient pointed at the copy.
    """
    asset = g.existing(GRADIENT_ASSET)
    if asset is None:
        asset = g.tools().create_asset('GA_MobWater', GRAD_ROOT, unreal.GradientAsset,
                                       unreal.GradientAssetFactory())

    layers = []
    for name, blend, stops in GRADIENT_ROWS:
        layer = unreal.GradientLayer()
        layer.set_editor_property('name', name)
        layer.set_editor_property('blend_space', blend)

        built = []
        for time, rgb, interp in stops:
            stop = unreal.GradientStop()
            stop.set_editor_property('time', float(time))
            stop.set_editor_property('color', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            stop.set_editor_property('interp', interp)
            built.append(stop)

        layer.set_editor_property('stops', built)
        layers.append(layer)

    asset.set_editor_property('width', 256)

    # LDR, not HDR. Nothing in a water palette belongs past white, and a quarter of the memory of an
    # RGBA16F atlas is the difference between a ramp costing nothing and costing enough to mention.
    asset.set_editor_property('format', unreal.GradientTextureFormat.LDR)

    # ALWAYS, because the bake runs off the property change notification and the default mode does
    # not raise one - the rows would be written and the texture would still be whatever it was.
    asset.set_editor_property('gradients', layers, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    g.save(asset)

    texture = asset.get_editor_property('texture')
    if texture is None:
        raise RuntimeError('GA_MobWater did not bake a texture')

    g.save(texture)

    return [name for name, _, _ in GRADIENT_ROWS]


# ---------------------------------------------------------------------------
# The master material
# ---------------------------------------------------------------------------

MASTER_NAME = 'M_MobWater'

INCLUDES = ['/MobWater/Public/MobWaterSurface.ush', '/MobWater/Public/MobWaterField.ush',
            '/MobWater/Public/MobWaterSpectrum.ush']

RIPPLE_FIELD_SIZE = 256

# Custom primitive data indices. These have to agree with MobWaterTypes.h; mob_water_verify asserts
# that they do, because nothing else would notice if one moved.
#
# There are thirty six and no more - the engine's FCustomPrimitiveData is nine float4s, and a write
# past the end is discarded without a word. Anything new pairs into an existing slot.
CPD_SHALLOW_COLOR = 0
CPD_DEEP_COLOR = 3

# The gradient fork replaces both colours, so its row rides in the first of the six floats they would
# have taken. Only one of the two is ever compiled, so the slot is never wanted twice.
CPD_GRADIENT_ROW = 0
CPD_FADE_DEPTH = 6
CPD_CLARITY_DEPTH = 7
CPD_SHORE_FOAM_DEPTH = 8
CPD_CREST_FOAM_THRESHOLD = 9
CPD_WAVE_AMPLITUDE = 10
CPD_SHORE_FADE_DISTANCE = 11
CPD_REFRACTION_STRENGTH = 12
CPD_RIPPLE_STRENGTH = 13
CPD_ROUGHNESS = 14
CPD_FLOW_VELOCITY = 15
CPD_HALF_EXTENT = 17
CPD_DETAIL_STRENGTH = 19
CPD_CAUSTIC_STRENGTH = 20
CPD_CAUSTIC_DEPTH = 21
CPD_FOAM_NOISE_AMOUNT = 22
CPD_FOAM_SHARPNESS = 23

# Band count in the whole part, band gap in the fraction. MobWaterFoamBands splits them.
CPD_FOAM_BANDS = 24

CPD_GLINT_GLOSS = 25
CPD_GLINT_STRENGTH = 26
CPD_GLINT_THRESHOLD = 27
CPD_DETAIL_SCROLL_SPEED = 28
CPD_MACRO_STRENGTH = 29
# Edge line width in hundredths in the whole part, foam opacity in the fraction.
CPD_EDGE_FOAM_WIDTH = 30
CPD_MIN_OPACITY = 31
CPD_REFLECTION_STRENGTH = 32
CPD_GLINT_DENSITY = 33
CPD_GLINT_EMISSIVE = 34
CPD_UNLIT = 35

# How large the two detail normal tilings are, in world units. Not per body: an artist resizing a
# pond must not resize the ripples on it, and two bodies side by side have to match.
#
# Metres rather than tens of centimetres. Ripples the size of a hand read as scales from anywhere
# further away than a hand, and a pond covered in them looks like a texture rather than like water.
DETAIL_SCALE_A = 700.0
DETAIL_SCALE_B = 290.0

# The slow swell. Tens of metres, so it never reads as texture - it is what makes one part of a lake
# look different from another rather than uniformly busy.
DETAIL_SCALE_MACRO = 4200.0

# How large a caustic cell is on the bed, in world units.
CAUSTIC_SCALE = 320.0


# The whole wave set in one node. Sixteen vectors in, three things out.
#
# One Custom node rather than one per wave: a Custom node emits its whole body regardless of what
# reads it, so eight of them would be eight copies of the set-wide scaling as well as the waves.
_CODE_WAVES = """
float3 Disp = float3(0.0f, 0.0f, 0.0f);
float3 Nrm = float3(0.0f, 0.0f, 1.0f);
float Fold = 0.0f;

MobWaterEvaluate(
	A0, B0, A1, B1, A2, B2, A3, B3,
	A4, B4, A5, B5, A6, B6, A7, B7,
	SampleXY, Time,
	Scales.y, Scales.z, Scales.w, Scales.x,
	Disp, Nrm, Fold);

WaveNormal = Nrm;
WaveFold = Fold;
return Disp;
"""

# Where in the baked atlas this vertex is, at the two frames the loop is between.
#
# Both frames share a column, so what this really returns is one column and two rows. It is computed
# in the vertex shader for the displacement and again in the pixel shader for the normal, because the
# ocean's mesh reaches the horizon and a normal carried across one of its triangles is a normal for
# something the size of a house.
_CODE_SPECTRUM_UV = """
float Blend = 0.0f;
float4 UV = MobWaterSpectrumUV(WorldXY, Time, Params, Scale.w, Blend);
SpectrumBlend = Blend;
return UV;
"""

_CODE_SPECTRUM_DISPLACEMENT = """
float Fold = 0.0f;
float3 Out = MobWaterSpectrumDisplacement(S0, S1, Blend, float3(Scale.x, Scale.x, Scale.y), Fold);
SpectrumFold = Fold;
return Out;
"""

_CODE_SPECTRUM_NORMAL = """
return MobWaterSpectrumNormal(N0, N1, Blend, Scale.z);
"""

_CODE_SPECTRUM_COMBINE_NORMAL = """
return MobWaterBlendNormals(WaveNormal, SpectrumNormal);
"""

# Two nodes rather than one with a branch, because a Custom node's body is emitted whether or not
# the switch above it chose this one.
_CODE_SHORE_BOX = """
return MobWaterShoreAttenuation(MobWaterEdgeDistanceBox(UV, HalfExtent), FadeDistance);
"""

_CODE_SHORE_RADIAL = """
return MobWaterShoreAttenuation(MobWaterEdgeDistanceRadial(UV, HalfExtent), FadeDistance);
"""

_CODE_SHORE_VERTEX = """
return MobWaterShoreAttenuation(MobWaterEdgeDistanceVertex(VertexShore), FadeDistance);
"""

# Foam coordinates, in the shoreline's frame rather than the water's. Each shape works out its own
# distance to the bank again rather than being handed the attenuation the waves use: that one has
# already been divided by the fade distance and clamped, and what these need is the raw distance.
_CODE_FOAM_UV_BOX = """
return MobWaterShoreUVsBox(UV, HalfExtent, MobWaterEdgeDistanceBox(UV, HalfExtent), Scale);
"""

_CODE_FOAM_UV_RADIAL = """
return MobWaterShoreUVsRadial(UV, HalfExtent, MobWaterEdgeDistanceRadial(UV, HalfExtent), Scale);
"""

_CODE_FOAM_UV_SPLINE = """
return MobWaterShoreUVsSpline(UV, MobWaterEdgeDistanceVertex(VertexShore), Scale);
"""

_CODE_FOAM_TEXTURE_AMOUNT = """
float NoiseAmount, TextureAmount;
MobWaterUnpackFoamNoise(Packed, NoiseAmount, TextureAmount);
return TextureAmount;
"""

_CODE_COLUMN = """
return MobWaterColumn(SceneDepth, PixelDepth);
"""

_CODE_EXCLUSION = """
return MobWaterExclusion(WorldXY, A0, B0, A1, B1, A2, B2, A3, B3, Softness);
"""

_CODE_GLINT = """
return MobWaterSunGlint(Normal, ViewDirection, SunDirection.xyz, SunColor.rgb, Gloss, Strength,
	Threshold, Break, Density, Emissive);
"""

# Where the reflection is read from, and how much of it there is.
#
# Params is (Intensity, Rotation, unused, unused), published by the subsystem so a level can turn its
# sky to match its sun without touching twenty-four material instances.
_CODE_REFLECTION_UV = """
return MobWaterLongLatUV(ReflectionVector, Params.y);
"""

_CODE_REFLECTION = """
return Sky * (MobWaterFresnel(Normal, ViewDirection) * Params.x * BodyStrength);
"""

# Every term the surface is built from, one at a time.
#
# A surface made of nine things multiplied together cannot be debugged by adjusting them: whatever is
# wrong looks like whatever else you happened to change. MobMaterials has these for the same reason.
_CODE_DEBUG = """
if (Mode < 1.5f) return float3(Column / 500.0f, Column / 500.0f, Column / 500.0f);
if (Mode < 2.5f) return float3(Foam, Foam, Foam);
if (Mode < 3.5f) return Normal * 0.5f + 0.5f;
if (Mode < 4.5f) return float3(Opacity, Opacity, Opacity);
if (Mode < 5.5f) return float3(ShoreFade, ShoreFade, ShoreFade);
if (Mode < 6.5f) return float3(Caustics, Caustics, Caustics);
if (Mode < 7.5f) return Glint;
if (Mode < 8.5f) return Reflection;
return float3(Fold, Fold, Fold);
"""

_CODE_COLOR = """
return MobWaterAbsorb(ShallowColor, DeepColor, Column, FadeDepth);
"""

_CODE_GRADIENT_COORD = """
return MobWaterGradientCoord(Column, FadeDepth);
"""

_CODE_OPACITY = """
return MobWaterOpacity(Column, ClarityDepth, MinOpacity);
"""

_CODE_DETAIL_UV = """
float2 UVA, UVB;
MobWaterDetailUVs(WorldXY, Time, ScaleA, ScaleB, Flow, ScrollSpeed, UVA, UVB);
UVSecond = UVB;
return UVA;
"""

_CODE_COMBINE_NORMAL = """
float3 Detail = MobWaterBlendNormals(DetailA, DetailB);

// The macro layer is the slow swell across a whole body: too large to read as texture, too small to
// be a wave. It is most of what stops a surface looking uniformly busy everywhere.
float3 Macro = DetailC;
Macro.xy *= MacroStrength;

Detail = MobWaterBlendNormals(Detail, normalize(Macro));
Detail.xy *= Strength;

return MobWaterBlendNormals(WaveNormal, normalize(Detail));
"""

_CODE_CAUSTIC_UV = """
float2 UVA, UVB;
float3 Bed = MobWaterBedPosition(SurfaceWorld, CameraVector, Column);
MobWaterCausticUVs(Bed.xy, Time, Scale, Flow, ScrollSpeed, UVA, UVB);
UVSecond = UVB;
return UVA;
"""

_CODE_CAUSTICS = """
return MobWaterCaustics(LayerA, LayerB, Column, FadeDepth, ShoreFade, Strength);
"""

_CODE_FOAM = """
float NoiseAmount, TextureAmount;
MobWaterUnpackFoamNoise(NoisePacked, NoiseAmount, TextureAmount);

float EdgeWidth, Opacity;
MobWaterUnpackFoamEdge(EdgePacked, EdgeWidth, Opacity);

// The noise moves where each band ends rather than how bright it is. Foam is white; what varies is
// where it stops, and multiplying noise into the result instead is what turns a shoreline into
// scales lying on the water.
float Shore = MobWaterFoamBand(Column, ShoreFoamDepth, Noise, NoiseAmount, Sharpness);
float Edge = MobWaterFoamBand(ShoreFade, EdgeWidth, Noise, NoiseAmount, Sharpness);
float Crest = MobWaterCrestFoam(Fold, CrestFoamThreshold);

float Foam = max(max(Shore, Edge), Crest);

// A wake is not a contour, so it goes in after the banding rather than through it. Banding rounds
// every band up, which means the faintest trace of foam lands on the first step: a wake worth a
// fifth of white comes out as a solid slab of it with a hard edge, dragged along behind whatever
// left it. The shoreline survives banding because its input is a gradient across the band's own
// width, and stepping a gradient is the whole point of it.
//
// Scaled last, after the banding, so turning the foam down thins every band evenly rather than
// eating them one at a time from the shore outwards.
return max(MobWaterFoamBands(Foam, Bands), RippleFoam) * Opacity;
"""

# The ripple field, read back as a normal rather than as a height.
#
# Its own Custom node behind the static switch, not an output on a shared one: a Custom node emits
# its whole body whether or not anything reads it, so folding this into the normal combine would
# leave five texture samples running on every body of water that has ripples turned off.
_CODE_RIPPLES = """
float Height; float2 Slope; float Foam;
MobWaterRippleSample(Field, FieldSampler, WorldXY, Area, TexelSize, Height, Slope, Foam);

RippleFoam = Foam;

// Slope is a height difference across two texels, so it is already the derivative the normal wants.
//
// Eight, not forty. Forty was arrived at when the field did not propagate and its slopes were a
// rounding error, so the gain was wound up until something showed. A field that actually carries a
// wave produces real slopes, and forty tilts a crest to nearly vertical - which is invisible on the
// water itself and catastrophic through the glint, because every crest then crosses the highlight
// threshold at once and the wake comes out as a spray of hard white shards.
float3 Ripple = normalize(float3(-Slope * Strength * 8.0f, 1.0f));
return MobWaterBlendNormals(Base, Ripple);
"""


def _wave_node(mat, collection, sample_xy, time, x, y):
    """The Custom node that evaluates the wave set, and the collection nodes feeding it."""
    inputs = []
    sources = []

    for i in range(MAX_WAVES):
        sources.append(g.collection_param(mat, collection, 'WaveA%d' % i, x - 2, y + i * 2))
        inputs.append('A%d' % i)
        sources.append(g.collection_param(mat, collection, 'WaveB%d' % i, x - 2, y + i * 2 + 1))
        inputs.append('B%d' % i)

    scales = g.collection_param(mat, collection, 'WaveScales', x - 2, y - 2)

    inputs += ['SampleXY', 'Time', 'Scales']

    node = g.custom(mat, _CODE_WAVES, g.CMOT.CMOT_FLOAT3, inputs,
                    [('WaveNormal', g.CMOT.CMOT_FLOAT3), ('WaveFold', g.CMOT.CMOT_FLOAT1)],
                    x, y, 'The wave set, evaluated. Mirrors FMobWaterWaves::Evaluate exactly.',
                    includes=INCLUDES)

    for name, src in zip(inputs, sources + [sample_xy, time, scales]):
        g.link(src, '', node, name)

    return node


SPECTRUM_ROOT = mob_water_spectrum.SPECTRUM_ROOT


def _atlas_tap(mat, obj, coords, x, y):
    """One read of a frame atlas, at mip zero and nothing else.

    The explicit level is what makes the layout safe, and it is not an optimisation. The frames sit
    side by side, so any filter that widens its footprint reads the neighbouring frame - and the
    shared wrap sampler is anisotropic, which on a surface seen at a grazing angle is exactly that.
    It presents as the sea flattening towards the horizon rather than as a filtering choice, because
    what an average of several frames of a travelling wave comes to is nothing.

    Asking for a level rather than a derivative also settles the vertex shader's own question. There
    are no derivatives there, and a sample with no level named is a sample the compiler has to invent
    one for.
    """
    tap = g.expr(mat, unreal.MaterialExpressionTextureSample, x, y)
    tap.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    tap.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    tap.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_MIP_LEVEL)

    g.link(coords, '', tap, 'UVs')
    g.link(obj, '', tap, 'Tex')
    g.link(g.const(mat, 0.0, x, y + 1), '', tap, 'Level')

    return _rgba(mat, tap, x + 1, y)


def _rgba(mat, tap, x, y):
    """A texture sample as a genuine float4.

    A TextureSample's unnamed output is RGB, and it has no float4 output that can be connected by
    name. Handing that to a Custom node whose parameter is a float4 fails inside the generated
    material with "cannot implicitly convert from float3 to float4", pointing at a line of HLSL
    nobody wrote - so the alpha is appended back on here rather than discovered there.
    """
    appended = g.expr(mat, unreal.MaterialExpressionAppendVector, x, y)
    g.link(tap, '', appended, 'A')
    g.link(tap, 'A', appended, 'B')
    return appended


def _spectrum_nodes(mat, collection, sample_xy, time, x, y):
    """The baked sea state, read.

    Returns (displacement, fold, normal). Four taps: two frames of displacement in the vertex shader
    and two of normal in the pixel one. Every one of them is Shared:Wrap, so the whole thing costs no
    sampler at all - what it costs is four texture reads and the arithmetic to place them.
    """
    params = g.collection_param(mat, collection, 'SpectrumParams', x - 2, y)
    scale = g.collection_param(mat, collection, 'SpectrumScale', x - 2, y + 1)

    uv = g.custom(mat, _CODE_SPECTRUM_UV, g.CMOT.CMOT_FLOAT4,
                  ['WorldXY', 'Time', 'Params', 'Scale'],
                  [('SpectrumBlend', g.CMOT.CMOT_FLOAT1)], x, y,
                  'Where in the atlas this point is, at the two frames the loop is between.',
                  includes=INCLUDES)
    g.link(sample_xy, '', uv, 'WorldXY')
    g.link(time, '', uv, 'Time')
    g.link(params, '', uv, 'Params')
    g.link(scale, '', uv, 'Scale')

    def frame_uv(component_b, offset):
        mask = g.expr(mat, unreal.MaterialExpressionComponentMask, x + 1, y + offset)
        mask.set_editor_property('r', not component_b)
        mask.set_editor_property('g', not component_b)
        mask.set_editor_property('b', component_b)
        mask.set_editor_property('a', component_b)
        g.link(uv, '', mask, '')
        return mask

    uv0 = frame_uv(False, 0)
    uv1 = frame_uv(True, 1)

    def atlas(name, texture, description, at_y):
        # A texture object read by two samples rather than two sampler parameters of the same name.
        # One parameter means an instance pointing at a project's own bake cannot leave one of the
        # two frames addressing the atlas it replaced.
        obj = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, x + 1, at_y)
        obj.set_editor_property('parameter_name', name)
        obj.set_editor_property('texture', texture)
        obj.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        obj.set_editor_property('group', 'Waves')
        obj.set_editor_property('desc', description)

        return [_atlas_tap(mat, obj, coords, x + 2, at_y + index * 2)
                for index, coords in enumerate((uv0, uv1))]

    displacement_taps = atlas('SpectrumDisplacement', g.existing(SPECTRUM_ROOT + '/T_MobWaterSpectrum'),
                              'Where the baked sea moves a point, and how hard it is folding there. '
                              'Point an instance at a bake of your own to change the sea.', y + 3)

    normal_taps = atlas('SpectrumNormal', g.existing(SPECTRUM_ROOT + '/T_MobWaterSpectrumNormal'),
                        "The baked sea's own slope. Read per pixel, because an ocean's triangles "
                        'are the size of houses.', y + 6)

    displacement = g.custom(mat, _CODE_SPECTRUM_DISPLACEMENT, g.CMOT.CMOT_FLOAT3,
                            ['S0', 'S1', 'Blend', 'Scale'],
                            [('SpectrumFold', g.CMOT.CMOT_FLOAT1)], x + 3, y + 3,
                            'The baked displacement, blended across the two frames and decoded.',
                            includes=INCLUDES)
    g.link(displacement_taps[0], '', displacement, 'S0')
    g.link(displacement_taps[1], '', displacement, 'S1')
    g.link(uv, 'SpectrumBlend', displacement, 'Blend')
    g.link(scale, '', displacement, 'Scale')

    normal = g.custom(mat, _CODE_SPECTRUM_NORMAL, g.CMOT.CMOT_FLOAT3,
                      ['N0', 'N1', 'Blend', 'Scale'], [], x + 3, y + 6,
                      'The baked slope, blended and rebuilt into a normal.', includes=INCLUDES)
    g.link(normal_taps[0], '', normal, 'N0')
    g.link(normal_taps[1], '', normal, 'N1')
    g.link(uv, 'SpectrumBlend', normal, 'Blend')
    g.link(scale, '', normal, 'Scale')

    return displacement, normal


def build_master_material():
    """M_MobWater: the surface, as one translucent material reading the depth buffer."""
    collection = g.existing(g.MAT_ROOT + '/' + COLLECTION_NAME)
    if collection is None:
        raise RuntimeError('build the parameter collection before the master material')

    mat = g.get_or_create_material(g.MAT_ROOT, MASTER_NAME)

    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # Two sided, because the underside of the surface is a view anyone standing in the water gets.
    # It also means the generated meshes' triangle winding cannot make a body invisible, which is a
    # failure that looks exactly like a broken material.
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('translucency_lighting_mode',
                            unreal.TranslucencyLightingMode.TLM_SURFACE)
    # The wave normal is already in world space, and converting it to tangent space to have the
    # renderer convert it straight back is arithmetic nobody reads.
    # Tangent space, and the wave normal is transformed into it at the end.
    #
    # It would be cheaper to leave the normal in world space and skip the transform, and it cannot be
    # done: Pixel Normal Offset refraction is the only mode fit for a surface this large - the engine
    # says so where the mode is declared, because Index Of Refraction reads outside scene colour on
    # anything big - and that mode is valid only with tangent space normals.
    mat.set_editor_property('tangent_space_normal', True)
    mat.set_editor_property('refraction_method', unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)

    # --- waves, and where the vertex goes -----------------------------------
    #
    # WPT_ExcludeAllShaderOffsets rather than the default: this is the position the vertex started
    # at, and feeding it a position that already includes the offset makes the wave chase itself.
    world_pos = g.expr(mat, unreal.MaterialExpressionWorldPosition, -6, 0)
    world_pos.set_editor_property('world_position_shader_offset',
                                  unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)

    sample_xy = g.expr(mat, unreal.MaterialExpressionComponentMask, -5, 0)
    sample_xy.set_editor_property('r', True)
    sample_xy.set_editor_property('g', True)
    sample_xy.set_editor_property('b', False)
    sample_xy.set_editor_property('a', False)
    g.link(world_pos, '', sample_xy, '')

    # The clock, shared by the waves and by everything that drifts with them. One node, so the
    # surface detail and the surface shape can never be a frame out of step with each other.
    time_param = g.collection_param(mat, collection, 'Time', -5, 2)

    waves = _wave_node(mat, collection, sample_xy, time_param, -3, 0)

    # --- and the sea that was solved offline --------------------------------
    #
    # Added to the Gerstner set rather than replacing it, so an ocean whose spectrum has not been
    # baked yet still has waves instead of turning to glass, and a level can put a long authored
    # swell under a baked chop.
    spectrum_disp, spectrum_normal = _spectrum_nodes(mat, collection, sample_xy, time_param, -3, 18)

    b_spectrum = g.static_bool(mat, 'bSpectrum', False, 'Waves', -2, 17, 13,
                               'Adds a baked sea state on top of the wave set. Off, the four atlas '
                               'reads and their addressing leave the shader entirely, which is why '
                               'only the ocean instances carry it.')

    # --- how much of the wave survives this close to the bank ---------------
    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -5, 24)

    half_extent_vec = g.cpd_vector(mat, 'HalfExtent', (500.0, 500.0, 0.0, 0.0), CPD_HALF_EXTENT,
                                   'Shape', -5, 26,
                                   'Half the body size on X and Y. The vertex shader has no other '
                                   'way to know how far it is from the bank.')

    half_extent = g.expr(mat, unreal.MaterialExpressionComponentMask, -4, 26)
    half_extent.set_editor_property('r', True)
    half_extent.set_editor_property('g', True)
    half_extent.set_editor_property('b', False)
    half_extent.set_editor_property('a', False)
    g.link(half_extent_vec, '', half_extent, '')

    fade_distance = g.cpd_scalar(mat, 'ShoreFadeDistance', 200.0, CPD_SHORE_FADE_DISTANCE,
                                 'Shape', -5, 28,
                                 'How far in from the edge the waves are flattened to nothing.')

    shore_box = g.custom(mat, _CODE_SHORE_BOX, g.CMOT.CMOT_FLOAT1,
                         ['UV', 'HalfExtent', 'FadeDistance'], [], -3, 24,
                         'Distance to a rectangular bank.', includes=INCLUDES)
    g.link(uv, '', shore_box, 'UV')
    g.link(half_extent, '', shore_box, 'HalfExtent')
    g.link(fade_distance, '', shore_box, 'FadeDistance')

    shore_radial = g.custom(mat, _CODE_SHORE_RADIAL, g.CMOT.CMOT_FLOAT1,
                            ['UV', 'HalfExtent', 'FadeDistance'], [], -3, 30,
                            'Distance to a circular bank.', includes=INCLUDES)
    g.link(uv, '', shore_radial, 'UV')
    g.link(half_extent, '', shore_radial, 'HalfExtent')
    g.link(fade_distance, '', shore_radial, 'FadeDistance')

    # A spline body's shape is whatever was drawn, so its distance to the bank is baked per vertex
    # rather than worked out from an extent. Red carries it; nothing reads the other channels.
    vertex_colour = g.expr(mat, unreal.MaterialExpressionVertexColor, -4, 33)

    shore_vertex = g.custom(mat, _CODE_SHORE_VERTEX, g.CMOT.CMOT_FLOAT1,
                            ['VertexShore', 'FadeDistance'], [], -3, 33,
                            'Distance to a drawn shoreline, carried by the mesh.', includes=INCLUDES)
    g.link(vertex_colour, 'R', shore_vertex, 'VertexShore')
    g.link(fade_distance, '', shore_vertex, 'FadeDistance')

    b_radial = g.static_bool(mat, 'bRadialShore', False, 'Shape', -4, 32, 10,
                             'A disc rather than a rectangle. Changes only which distance the waves '
                             'lie down against.')

    b_vertex_shore = g.static_bool(mat, 'bShoreFromVertex', False, 'Shape', -4, 34, 11,
                                   'The distance to the bank comes from the mesh rather than from an '
                                   'extent. What a spline body uses; it overrides the disc switch.')

    shore_analytic = g.static_switch(mat, b_radial, shore_radial, '', shore_box, '', -2, 27)
    shore = g.static_switch(mat, b_vertex_shore, shore_vertex, '', shore_analytic, '', -2, 31)

    amplitude = g.cpd_scalar(mat, 'WaveAmplitude', 1.0, CPD_WAVE_AMPLITUDE, 'Waves', -3, 34,
                             'Scales this body-s waves on top of the set the world shares.')

    wave_scale = g.mul(mat, shore, '', amplitude, '', -1, 30)

    # The baked sea lies down at a bank on the same terms the authored waves do. An ocean sets no
    # fade distance so this is one multiply by one for it, and a body that did set one gets a
    # spectrum that stops at the shore rather than one that runs up the beach.
    displaced = g.add(mat, waves, '', spectrum_disp, '', -1, 18)
    displacement = g.static_switch(mat, b_spectrum, displaced, '', waves, '', -1, 20)

    wpo = g.mul(mat, displacement, '', wave_scale, '', 0, 0)

    # --- how much water is in front of what is behind it --------------------
    scene_depth = g.expr(mat, unreal.MaterialExpressionSceneDepth, -5, 40)
    pixel_depth = g.expr(mat, unreal.MaterialExpressionPixelDepth, -5, 42)

    column = g.custom(mat, _CODE_COLUMN, g.CMOT.CMOT_FLOAT1, ['SceneDepth', 'PixelDepth'], [],
                      -3, 41, 'Water between this pixel and the bed, along the view ray.',
                      includes=INCLUDES)
    g.link(scene_depth, '', column, 'SceneDepth')
    g.link(pixel_depth, '', column, 'PixelDepth')

    shallow = g.cpd_vector(mat, 'ShallowColor', (0.18, 0.42, 0.42, 1.0), CPD_SHALLOW_COLOR,
                           'Colour', -5, 44, 'The water where there is least of it.')
    deep = g.cpd_vector(mat, 'DeepColor', (0.01, 0.06, 0.11, 1.0), CPD_DEEP_COLOR,
                        'Colour', -5, 46, 'The water where there is most of it.')
    fade_depth = g.cpd_scalar(mat, 'FadeDepth', 300.0, CPD_FADE_DEPTH, 'Colour', -5, 48,
                              'The water column over which the colour reaches the deep one.')
    clarity_depth = g.cpd_scalar(mat, 'ClarityDepth', 500.0, CPD_CLARITY_DEPTH, 'Colour', -5, 50,
                                 'The water column over which the bed stops being visible.')

    base_color = g.custom(mat, _CODE_COLOR, g.CMOT.CMOT_FLOAT3,
                          ['ShallowColor', 'DeepColor', 'Column', 'FadeDepth'], [], -1, 45,
                          'Colour by how much water there is.', includes=INCLUDES)
    g.link(shallow, '', base_color, 'ShallowColor')
    g.link(deep, '', base_color, 'DeepColor')
    g.link(column, '', base_color, 'Column')
    g.link(fade_depth, '', base_color, 'FadeDepth')

    # --- or the same depth, as a coordinate into a ramp ----------------------
    #
    # Absorption is an exponential, so it can only ever be smooth. A ramp holds whatever is painted
    # into it, hard steps included, which is the whole difference between water that grades and water
    # that is drawn - and it is one tap either way, on the depth that was already recovered.
    gradient_coord = g.custom(mat, _CODE_GRADIENT_COORD, g.CMOT.CMOT_FLOAT1,
                              ['Column', 'FadeDepth'], [], -4, 45,
                              'How far down the ramp this much water is.', includes=INCLUDES)
    g.link(column, '', gradient_coord, 'Column')
    g.link(fade_depth, '', gradient_coord, 'FadeDepth')

    atlas = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -4, 46)
    atlas.set_editor_property('parameter_name', 'ColorGradient')
    atlas.set_editor_property('texture', g.existing(GRADIENT_TEXTURE))
    atlas.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    atlas.set_editor_property('group', 'Colour')
    atlas.set_editor_property('desc', 'The palettes this water can be graded by, one to a row. '
                                      'Point an instance at an atlas of its own to bring more.')

    gradient_row = g.cpd_scalar(mat, 'GradientRow', 0.0, CPD_GRADIENT_ROW, 'Colour', -4, 47,
                                'Which row of the atlas this body reads. Whole numbers only: a '
                                'fraction lands between two rows and the filter blends them. %s.'
                                % ', '.join('%d %s' % (i, n)
                                            for i, (n, _, _) in enumerate(GRADIENT_ROWS)))

    # The row is resolved at compile time when nothing is connected, which would bake this plugin's
    # own layout into the shader. Driven from a parameter instead, a project atlas only has to say
    # where its rows are rather than match an order it did not choose.
    ramp_uv = g.expr(mat, unreal.MaterialExpressionGradientCoordinate, -3, 46)
    ramp_uv.set_editor_property('gradient', g.existing(GRADIENT_ASSET))
    ramp_uv.set_editor_property('gradient_name', GRADIENT_ROWS[0][0])
    g.link(gradient_coord, '', ramp_uv, 'Time')
    g.link(atlas, '', ramp_uv, 'Atlas')
    g.link(gradient_row, '', ramp_uv, 'Row')

    # Sampled here rather than by a Sample Gradient node, which asks for the texture's own sampler
    # and spends one of the sixteen. Both this and the coordinate read the one texture object, so an
    # instance swapping the atlas cannot leave the row arithmetic addressing the old one's height.
    ramp = g.expr(mat, unreal.MaterialExpressionTextureSample, -2, 46)
    ramp.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    ramp.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    g.link(ramp_uv, 'UV', ramp, 'UVs')
    g.link(atlas, '', ramp, 'Tex')

    b_gradient = g.static_bool(mat, 'bGradientColor', False, 'Colour', -2, 47, 12,
                               'Grades the water along a ramp rather than between two colours. Off, '
                               'the atlas is not read and the ramp arithmetic leaves the shader.')

    base_color = g.static_switch(mat, b_gradient, ramp, '', base_color, '', 0, 46)

    min_opacity = g.cpd_scalar(mat, 'MinOpacity', 0.0, CPD_MIN_OPACITY, 'Colour', -5, 51,
                               'How opaque the water is regardless of how much of it there is. 0 '
                               'lets depth decide; 1 is flat and hides the bed entirely, which is '
                               'what a toon surface wants and what depth alone cannot give on a '
                               'shallow body.')

    opacity = g.custom(mat, _CODE_OPACITY, g.CMOT.CMOT_FLOAT1,
                       ['Column', 'ClarityDepth', 'MinOpacity'], [],
                       -1, 50, 'Opacity by how much water there is, with a floor.', includes=INCLUDES)
    g.link(column, '', opacity, 'Column')
    g.link(clarity_depth, '', opacity, 'ClarityDepth')
    g.link(min_opacity, '', opacity, 'MinOpacity')

    roughness = g.cpd_scalar(mat, 'Roughness', 0.02, CPD_ROUGHNESS, 'Surface', -1, 54,
                             'Calm water is nearly a mirror; this is what the specular reads.')

    # --- detail, so the surface is water rather than tinted glass -----------
    #
    # Interpolated rather than recomputed. The wave set is eight sines and eight cosines, and asking
    # the pixel shader for its normal again is hundreds of instructions to learn what the vertices
    # already worked out.
    wave_normal = g.vertex_interpolator(mat, waves, 'WaveNormal', 1, 6)

    spectrum_lit = g.custom(mat, _CODE_SPECTRUM_COMBINE_NORMAL, g.CMOT.CMOT_FLOAT3,
                            ['WaveNormal', 'SpectrumNormal'], [], 1, 8,
                            'The authored waves and the baked sea, tilted together.',
                            includes=INCLUDES)
    g.link(wave_normal, '', spectrum_lit, 'WaveNormal')
    g.link(spectrum_normal, '', spectrum_lit, 'SpectrumNormal')

    wave_normal = g.static_switch(mat, b_spectrum, spectrum_lit, '', wave_normal, '', 1, 9)

    flow_vec = g.cpd_vector(mat, 'FlowVelocity', (0.0, 0.0, 0.0, 0.0), CPD_FLOW_VELOCITY,
                            'Surface', -5, 56, 'How fast the surface slides across the ground.')

    flow = g.expr(mat, unreal.MaterialExpressionComponentMask, -4, 56)
    flow.set_editor_property('r', True)
    flow.set_editor_property('g', True)
    flow.set_editor_property('b', False)
    flow.set_editor_property('a', False)
    g.link(flow_vec, '', flow, '')

    scroll_speed = g.cpd_scalar(mat, 'DetailScrollSpeed', 1.0, CPD_DETAIL_SCROLL_SPEED, 'Surface',
                                -4, 57,
                                'How fast the surface detail drifts on its own, with no flow and no '
                                'waves. 0 is genuinely still water.')

    detail_uv = g.custom(mat, _CODE_DETAIL_UV, g.CMOT.CMOT_FLOAT2,
                         ['WorldXY', 'Time', 'ScaleA', 'ScaleB', 'Flow', 'ScrollSpeed'],
                         [('UVSecond', g.CMOT.CMOT_FLOAT2)], -3, 56,
                         'Two sets of coordinates that never come back into step.', includes=INCLUDES)
    g.link(sample_xy, '', detail_uv, 'WorldXY')
    g.link(time_param, '', detail_uv, 'Time')
    g.link(g.const(mat, DETAIL_SCALE_A, -4, 58), '', detail_uv, 'ScaleA')
    g.link(g.const(mat, DETAIL_SCALE_B, -4, 59), '', detail_uv, 'ScaleB')
    g.link(flow, '', detail_uv, 'Flow')
    g.link(scroll_speed, '', detail_uv, 'ScrollSpeed')

    macro_uv = g.custom(mat, _CODE_DETAIL_UV, g.CMOT.CMOT_FLOAT2,
                        ['WorldXY', 'Time', 'ScaleA', 'ScaleB', 'Flow', 'ScrollSpeed'],
                        [('UVSecond', g.CMOT.CMOT_FLOAT2)], -3, 61,
                        'The slow swell across a whole body of water.', includes=INCLUDES)
    g.link(sample_xy, '', macro_uv, 'WorldXY')
    g.link(time_param, '', macro_uv, 'Time')
    g.link(g.const(mat, DETAIL_SCALE_MACRO, -4, 62), '', macro_uv, 'ScaleA')
    g.link(g.const(mat, DETAIL_SCALE_MACRO * 1.7, -4, 63), '', macro_uv, 'ScaleB')
    g.link(flow, '', macro_uv, 'Flow')
    g.link(scroll_speed, '', macro_uv, 'ScrollSpeed')

    normal_texture = g.existing(g.TEX_ROOT + '/T_MobWaterNormal')

    detail_a = g.texture_param(mat, 'DetailNormal', normal_texture, 'Surface', -2, 56,
                               g.ST.SAMPLERTYPE_NORMAL, detail_uv, '',
                               'The surface detail, tiled at the larger of the two scales.')
    detail_b = g.texture_param(mat, 'DetailNormalSecond', normal_texture, 'Surface', -2, 60,
                               g.ST.SAMPLERTYPE_NORMAL, detail_uv, 'UVSecond',
                               'The same texture at the smaller scale. One parameter would be one '
                               'tiling, and one tiling is a pattern sliding past.')

    detail_strength = g.cpd_scalar(mat, 'DetailStrength', 1.0, CPD_DETAIL_STRENGTH, 'Surface', -2, 63,
                                   'How much of the detail reaches the surface. Most of the '
                                   'difference between a stylized surface and a realistic one.')

    detail_macro = g.texture_param(mat, 'DetailNormalMacro', normal_texture, 'Surface', -2, 61,
                                   g.ST.SAMPLERTYPE_NORMAL, macro_uv, '',
                                   'The same texture again at tens of metres. Too large to read as '
                                   'texture, too small to be a wave, and most of what stops a body '
                                   'of water looking uniformly busy.')

    macro_strength = g.cpd_scalar(mat, 'MacroNormalStrength', 2.2, CPD_MACRO_STRENGTH, 'Surface',
                                  -2, 64, 'How pronounced the slow swell across the whole body is.')

    combined_normal = g.custom(mat, _CODE_COMBINE_NORMAL, g.CMOT.CMOT_FLOAT3,
                               ['WaveNormal', 'DetailA', 'DetailB', 'DetailC', 'MacroStrength',
                                'Strength'], [], -1, 58,
                               'The waves, the detail and the swell, without any of them flattening '
                               'the others.', includes=INCLUDES)
    g.link(wave_normal, '', combined_normal, 'WaveNormal')
    g.link(detail_a, '', combined_normal, 'DetailA')
    g.link(detail_b, '', combined_normal, 'DetailB')
    g.link(detail_macro, '', combined_normal, 'DetailC')
    g.link(macro_strength, '', combined_normal, 'MacroStrength')
    g.link(detail_strength, '', combined_normal, 'Strength')

    b_detail = g.static_bool(mat, 'bDetailNormals', True, 'Surface', -1, 64, 20,
                             'The two scrolling detail normals. Off, the surface has only the shape '
                             'of its own waves, which is the right look for hard stylized water and '
                             'costs two samples less.')

    detailed_normal = g.static_switch(mat, b_detail, combined_normal, '', wave_normal, '', 0, 58)

    # --- ripples ------------------------------------------------------------
    ripple_field = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -2, 65)
    ripple_field.set_editor_property('parameter_name', 'RippleField')
    ripple_field.set_editor_property('texture', g.existing(g.TEX_ROOT + '/RT_MobWaterRipple'))
    ripple_field.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    ripple_area = g.collection_param(mat, collection, 'RippleArea', -2, 66)

    ripple_strength = g.cpd_scalar(mat, 'RippleStrength', 1.0, CPD_RIPPLE_STRENGTH, 'Ripples', -2, 67,
                                   'How much of the field reaches this body. 0 is a still surface.')

    ripples = g.custom(mat, _CODE_RIPPLES, g.CMOT.CMOT_FLOAT3,
                       ['Field', 'WorldXY', 'Area', 'TexelSize', 'Strength', 'Base'],
                       [('RippleFoam', g.CMOT.CMOT_FLOAT1)], -1, 65,
                       'The interactive field, read back as a normal.', includes=INCLUDES)
    g.link(ripple_field, '', ripples, 'Field')
    g.link(sample_xy, '', ripples, 'WorldXY')
    g.link(ripple_area, '', ripples, 'Area')
    g.link(g.const(mat, 1.0 / RIPPLE_FIELD_SIZE, -2, 68), '', ripples, 'TexelSize')
    g.link(ripple_strength, '', ripples, 'Strength')
    g.link(detailed_normal, '', ripples, 'Base')

    b_ripples = g.static_bool(mat, 'bRipples', False, 'Ripples', -1, 69, 23,
                              'Reads the interactive field. Off, its five samples and their maths '
                              'leave the shader entirely.')

    final_normal = g.static_switch(mat, b_ripples, ripples, '', detailed_normal, '', 0, 65)
    ripple_foam = g.static_switch(mat, b_ripples, ripples, 'RippleFoam',
                                  g.const(mat, 0.0, -1, 70), '', 0, 70)

    # --- caustics on the bed ------------------------------------------------
    #
    # Drawn by the water rather than projected onto the ground by anything else. The surface has
    # already recovered where the bed is in order to know how much water is in front of it, so the
    # caustic costs a coordinate and two taps rather than a pass, a volume or a decal - none of which
    # this renderer has cheaply anyway.
    caustic_uv = g.custom(mat, _CODE_CAUSTIC_UV, g.CMOT.CMOT_FLOAT2,
                          ['SurfaceWorld', 'CameraVector', 'Column', 'Time', 'Scale', 'Flow',
                           'ScrollSpeed'],
                          [('UVSecond', g.CMOT.CMOT_FLOAT2)], -2, 41,
                          'Where the caustic lands on the bed.', includes=INCLUDES)
    caustic_view = g.expr(mat, unreal.MaterialExpressionCameraVectorWS, -3, 42)

    g.link(world_pos, '', caustic_uv, 'SurfaceWorld')
    g.link(caustic_view, '', caustic_uv, 'CameraVector')
    g.link(column, '', caustic_uv, 'Column')
    g.link(time_param, '', caustic_uv, 'Time')
    g.link(g.const(mat, CAUSTIC_SCALE, -3, 43), '', caustic_uv, 'Scale')
    g.link(flow, '', caustic_uv, 'Flow')
    g.link(scroll_speed, '', caustic_uv, 'ScrollSpeed')

    caustic_texture = g.existing(g.TEX_ROOT + '/T_MobWaterCaustics')

    caustic_a = g.texture_param(mat, 'Caustics', caustic_texture, 'Caustics', -1, 41,
                                g.ST.SAMPLERTYPE_MASKS, caustic_uv, '',
                                'The caustic web thrown onto whatever the water is sitting on.')
    caustic_b = g.texture_param(mat, 'CausticsSecond', caustic_texture, 'Caustics', -1, 43,
                                g.ST.SAMPLERTYPE_MASKS, caustic_uv, 'UVSecond',
                                'The second layer. One alone is a texture sliding over the ground.')

    caustic_fade = g.cpd_scalar(mat, 'CausticDepth', 400.0, CPD_CAUSTIC_DEPTH, 'Caustics', -1, 44,
                                'The water column over which the caustic is lost. Focused light stops '
                                'being focused as the water deepens.')
    caustic_strength = g.cpd_scalar(mat, 'CausticStrength', 0.7, CPD_CAUSTIC_STRENGTH, 'Caustics',
                                    -1, 45, 'How bright the caustic is. 0 is none.')

    caustics = g.custom(mat, _CODE_CAUSTICS, g.CMOT.CMOT_FLOAT1,
                        ['LayerA', 'LayerB', 'Column', 'FadeDepth', 'ShoreFade', 'Strength'], [],
                        0, 42, 'The caustic that reaches this pixel.', includes=INCLUDES)
    g.link(caustic_a, 'R', caustics, 'LayerA')
    g.link(caustic_b, 'R', caustics, 'LayerB')
    g.link(column, '', caustics, 'Column')
    g.link(caustic_fade, '', caustics, 'FadeDepth')
    g.link(shore, '', caustics, 'ShoreFade')
    g.link(caustic_strength, '', caustics, 'Strength')

    b_caustics = g.static_bool(mat, 'bCaustics', True, 'Caustics', 0, 46, 26,
                               'Throws a caustic onto the bed. Off, its two samples and the bed '
                               'reconstruction leave the shader entirely.')

    # Added to the water colour rather than to the emissive, so the caustic is light in the water and
    # is taken away with it as the body deepens - not a glow sitting on top of a lake.
    caustic_lit = g.add(mat, base_color, '', caustics, '', 1, 42)
    base_color = g.static_switch(mat, b_caustics, caustic_lit, '', base_color, '', 2, 42)

    # --- foam ---------------------------------------------------------------
    #
    # Both folds, added and held at one. The baked sea reports where its own transform compressed the
    # surface, which is where open water actually breaks white - and that is a different place from
    # where a Gerstner crest is steep, so a sum rather than a maximum.
    folded = g.saturate_expr(mat, g.add(mat, waves, 'WaveFold', spectrum_disp, 'SpectrumFold', 0, 7),
                             '', 0, 8)
    fold_source = g.static_switch(mat, b_spectrum, folded, '', waves, 'WaveFold', 0, 9)

    fold = g.vertex_interpolator(mat, fold_source, '', 1, 7)

    shore_foam_depth = g.cpd_scalar(mat, 'ShoreFoamDepth', 60.0, CPD_SHORE_FOAM_DEPTH, 'Foam', -5, 66,
                                    'How far up from the bed foam reaches. 0 is no shoreline foam.')
    crest_threshold = g.cpd_scalar(mat, 'CrestFoamThreshold', 0.55, CPD_CREST_FOAM_THRESHOLD, 'Foam',
                                   -5, 67, 'How hard the surface has to fold before it breaks white. '
                                           '1 is never, which is what a still body wants.')

    foam_texture = g.existing(g.TEX_ROOT + '/T_MobWaterFoam')
    foam_noise = g.texture_param(mat, 'FoamNoise', foam_texture, 'Foam', -2, 68,
                                 g.ST.SAMPLERTYPE_MASKS, detail_uv, '',
                                 'Breaks the foam into clumps. Shares the detail coordinates, so it '
                                 'drifts with the surface rather than sitting still under it.')

    edge_foam_width = g.cpd_scalar(mat, 'EdgeFoamWidth', 100.0, CPD_EDGE_FOAM_WIDTH, 'Foam', -2, 69,
                                   'Two values in one slot, because the primitive data is full. The '
                                   'whole part is how far in from the bank the edge line reaches in '
                                   'hundredths of the shore fade; the fraction is how opaque the foam '
                                   'is at all.')

    foam_noise_amount = g.cpd_scalar(mat, 'FoamNoiseAmount', 0.0, CPD_FOAM_NOISE_AMOUNT, 'Foam',
                                     -2, 70,
                                     'Two values in one slot, because the primitive data is full. The '
                                     'whole part is how far the noise moves the foam edge in '
                                     'hundredths of the band width; the fraction is how textured the '
                                     'foam is.')

    foam_texture_amount = g.custom(mat, _CODE_FOAM_TEXTURE_AMOUNT, g.CMOT.CMOT_FLOAT1,
                                   ['Packed'], [], -1, 74,
                                   'How textured the foam is, out of the packed slot.',
                                   includes=INCLUDES)
    g.link(foam_noise_amount, '', foam_texture_amount, 'Packed')
    foam_sharpness = g.cpd_scalar(mat, 'FoamSharpness', 3.0, CPD_FOAM_SHARPNESS, 'Foam', -2, 71,
                                  'How hard the foam edge is. Low is a wet fade up a beach, high is '
                                  'the cut line of a stylized surface.')
    foam_bands = g.cpd_scalar(mat, 'FoamBands', 0.0, CPD_FOAM_BANDS, 'Foam', -2, 72,
                              'Cuts the foam into this many steps, with the gap between them in the '
                              'fraction. 0 leaves it smooth; a small number gives the concentric '
                              'rings of a painted surface.')

    foam = g.custom(mat, _CODE_FOAM, g.CMOT.CMOT_FLOAT1,
                    ['Column', 'ShoreFoamDepth', 'ShoreFade', 'EdgePacked', 'Fold',
                     'CrestFoamThreshold', 'Noise', 'RippleFoam', 'NoisePacked', 'Sharpness',
                     'Bands'],
                    [], 1, 67, 'Foam at the shore, at the edge, on the crests, and behind whatever '
                               'went past.', includes=INCLUDES)
    g.link(column, '', foam, 'Column')
    g.link(shore_foam_depth, '', foam, 'ShoreFoamDepth')
    g.link(shore, '', foam, 'ShoreFade')
    g.link(edge_foam_width, '', foam, 'EdgePacked')
    g.link(fold, '', foam, 'Fold')
    g.link(crest_threshold, '', foam, 'CrestFoamThreshold')
    g.link(foam_noise, 'R', foam, 'Noise')
    g.link(ripple_foam, '', foam, 'RippleFoam')
    g.link(foam_noise_amount, '', foam, 'NoisePacked')
    g.link(foam_sharpness, '', foam, 'Sharpness')
    g.link(foam_bands, '', foam, 'Bands')

    # --- what the foam is made of -------------------------------------------
    #
    # Foam gets its own coordinates rather than the water's. The detail UVs scroll with the current,
    # so anything sampled in them streaks along the flow wherever the bank happens to be; foam
    # streaks the other way, away from the shore, because that is the direction the water ran up it.
    foam_uv_scale = g.scalar_param(mat, 'FoamTextureScale', 200.0, 'Foam', -3, 75,
                                   'The world size the foam texture tiles over, in centimetres. Not '
                                   'per body: two pools side by side have to agree, the same way the '
                                   'surface detail does.')

    foam_uv_box = g.custom(mat, _CODE_FOAM_UV_BOX, g.CMOT.CMOT_FLOAT2,
                           ['UV', 'HalfExtent', 'Scale'], [], -2, 74,
                           'Foam coordinates against a rectangular bank.', includes=INCLUDES)
    g.link(uv, '', foam_uv_box, 'UV')
    g.link(half_extent, '', foam_uv_box, 'HalfExtent')
    g.link(foam_uv_scale, '', foam_uv_box, 'Scale')

    foam_uv_radial = g.custom(mat, _CODE_FOAM_UV_RADIAL, g.CMOT.CMOT_FLOAT2,
                              ['UV', 'HalfExtent', 'Scale'], [], -2, 76,
                              'Foam coordinates against a circular bank.', includes=INCLUDES)
    g.link(uv, '', foam_uv_radial, 'UV')
    g.link(half_extent, '', foam_uv_radial, 'HalfExtent')
    g.link(foam_uv_scale, '', foam_uv_radial, 'Scale')

    foam_uv_spline = g.custom(mat, _CODE_FOAM_UV_SPLINE, g.CMOT.CMOT_FLOAT2,
                              ['UV', 'VertexShore', 'Scale'], [], -2, 78,
                              'Foam coordinates against a drawn shoreline.', includes=INCLUDES)
    g.link(uv, '', foam_uv_spline, 'UV')
    g.link(vertex_colour, 'R', foam_uv_spline, 'VertexShore')
    g.link(foam_uv_scale, '', foam_uv_spline, 'Scale')

    foam_uv_analytic = g.static_switch(mat, b_radial, foam_uv_radial, '', foam_uv_box, '', -1, 76)
    foam_uv = g.static_switch(mat, b_vertex_shore, foam_uv_spline, '', foam_uv_analytic, '', -1, 78)

    foam_own_texture = g.texture_param(mat, 'FoamTexture', foam_texture, 'Foam', 0, 76,
                                       g.ST.SAMPLERTYPE_MASKS, foam_uv, '',
                                       'The foam-s own pattern, sampled across the shoreline rather '
                                       'than with the water. Only read when bFoamTexture is on.')

    b_foam_texture = g.static_bool(mat, 'bFoamTexture', False, 'Foam', 0, 78, 27,
                                   'Foam carries a texture of its own, sampled in the shoreline-s '
                                   'frame. Off, the foam texture amount instead decides how much of '
                                   'the water underneath shows through it, and this sample and its '
                                   'coordinates leave the shader entirely.')

    # White, not off-white. A tint here is one more thing separating solid foam from the flat mark it
    # is meant to be, and a body that wants dirtier foam has a colour of its own to grade it with.
    white = g.const3(mat, (1.0, 1.0, 1.0), -1, 70)

    # The texture darkens the foam rather than cutting holes in it, so the band stays solid and only
    # its shading varies. Cutting the mask instead is what breaks a shoreline into scales.
    textured_foam = g.lerp(mat, white, '', foam_own_texture, 'R', foam_texture_amount, '', 0, 71)

    # Without a texture of its own the foam greys off towards the water's own froth colour as the
    # texture amount rises, so 1 lands exactly where the surface was before foam was made opaque.
    watery_foam = g.lerp(mat, white, '', g.const3(mat, (0.85, 0.90, 0.94), -1, 72), '',
                         foam_texture_amount, '', 0, 72)

    foam_color = g.static_switch(mat, b_foam_texture, textured_foam, '', watery_foam, '', 1, 71)

    foamed_color = g.lerp(mat, base_color, '', foam_color, '', foam, '', 0, 45)
    # Faded out at the very edge of the body, so the rim of a disc is water running out rather than
    # the silhouette of the mesh it is drawn on. Without this the edge is a polygon, and no amount of
    # tessellation hides a hard alpha boundary.
    edged_opacity = g.mul(mat, opacity, '', shore, '', 0, 49)

    foamed_opacity = g.saturate_expr(mat, g.add(mat, edged_opacity, '', foam, '', 0, 50), '', 1, 50)
    # Foam is froth, not water: it scatters rather than reflecting, so it takes the specular fully
    # off. Anything less leaves the water's shading showing through what is meant to be froth - and
    # letting it show is exactly what the texture amount asks for, so that winds the specular back
    # too. At 1 the foam shades as it did before it was made opaque.
    foam_gloss = g.lerp(mat, g.const(mat, 1.0, -1, 53), '', g.const(mat, 0.6, -1, 54), '',
                        foam_texture_amount, '', 0, 53)
    foamed_roughness = g.lerp(mat, roughness, '', foam_gloss, '', foam, '', 1, 54)

    b_foam = g.static_bool(mat, 'bFoam', True, 'Foam', -1, 72, 21,
                           'Shoreline and crest foam. Off, the two foam samples and their maths leave '
                           'the shader entirely.')

    out_color = g.static_switch(mat, b_foam, foamed_color, '', base_color, '', 2, 45)
    foam_opacity = g.static_switch(mat, b_foam, foamed_opacity, '', edged_opacity, '', 2, 50)
    out_roughness = g.static_switch(mat, b_foam, foamed_roughness, '', roughness, '', 2, 54)

    # How much water is left where the foam is, which is what every surface term below is scaled by.
    #
    # Replacing the colour is not enough on its own. The wave and detail normal still shade the foam,
    # the sun still glints off it and the sky still reflects in it, so the water's texture reads
    # straight through what is meant to be froth. Foam is a surface in its own right: where it is
    # solid, none of the water underneath it is visible, and that is what makes it read as drawn.
    #
    # With no texture of its own, the foam spends its texture amount on letting the water back
    # through instead: 0 is flat froth, 1 is the water shading straight through it.
    hidden = g.mul(mat, foam, '', g.sub(mat, g.const(mat, 1.0, 0, 56), '', foam_texture_amount, '',
                                        0, 57), '', 1, 56)
    hidden = g.static_switch(mat, b_foam_texture, foam, '', hidden, '', 1, 57)

    water_share = g.sub(mat, g.const(mat, 1.0, 1, 55), '', hidden, '', 2, 56)
    water_share = g.static_switch(mat, b_foam, water_share, '', g.const(mat, 1.0, 1, 58), '', 3, 56)

    # --- water kept out -----------------------------------------------------
    #
    # Not one of the enumerated variants, because it is four vectors of arithmetic and no samples -
    # cheap enough that carrying it everywhere costs less than doubling the instance count again.
    exclusion_inputs = ['WorldXY']
    exclusion_sources = [sample_xy]

    for i in range(EXCLUSION_SLOTS):
        for slot in ('A', 'B'):
            exclusion_sources.append(
                g.collection_param(mat, collection, 'Exclusion%s%d' % (slot, i), -3, 80 + i * 2))
            exclusion_inputs.append('%s%d' % (slot, i))

    exclusion_sources.append(g.collection_param(mat, collection, 'ExclusionSoftness', -3, 89))
    exclusion_inputs.append('Softness')

    exclusion = g.custom(mat, _CODE_EXCLUSION, g.CMOT.CMOT_FLOAT1, exclusion_inputs, [], -1, 84,
                         'How much water the nearest four exclusion volumes keep out of here.',
                         includes=INCLUDES)

    for name, src in zip(exclusion_inputs, exclusion_sources):
        g.link(src, '', exclusion, name)

    kept = g.sub(mat, g.const(mat, 1.0, -1, 90), '', exclusion, '', 0, 88)

    b_exclusion = g.static_bool(mat, 'bExclusion', True, 'Exclusion', -1, 91, 24,
                                'Lets exclusion volumes carve this body. Off, the four volumes are '
                                'not evaluated at all.')

    excluded_opacity = g.mul(mat, foam_opacity, '', kept, '', 1, 88)
    out_opacity = g.static_switch(mat, b_exclusion, excluded_opacity, '', foam_opacity, '', 2, 88)

    # --- refraction ---------------------------------------------------------
    #
    # The only feature that reads scene colour, and the one a platform may refuse outright, which is
    # why it is a compiled variant rather than a strength of zero.
    refraction_strength = g.cpd_scalar(mat, 'RefractionStrength', 0.3, CPD_REFRACTION_STRENGTH,
                                       'Refraction', -2, 74,
                                       'How far the surface bends what is behind it.')

    # In Pixel Normal Offset mode the refraction input scales the offset, and 1.0 means none while
    # 2.0 means a scale of one. It is not an index of refraction despite the pin's name, and feeding
    # water's 1.33 into it would ask for a third of the intended strength.
    water_offset = g.lerp(mat, g.const(mat, 1.0, -2, 76), '', g.const(mat, 2.0, -2, 77), '',
                          refraction_strength, '', -1, 76)

    b_refraction = g.static_bool(mat, 'bRefraction', False, 'Refraction', -1, 78, 22,
                                 'Bends what is behind the surface. Needs scene colour in the '
                                 'translucent pass; off, nothing reads it.')

    out_refraction = g.static_switch(mat, b_refraction, water_offset, '', g.const(mat, 1.0, -1, 79), '',
                                     1, 77)

    # --- the sun on it ------------------------------------------------------
    sun_direction = g.collection_param(mat, collection, 'SunDirection', -2, 94)
    sun_color = g.collection_param(mat, collection, 'SunColor', -2, 95)

    view_dir = g.expr(mat, unreal.MaterialExpressionCameraVectorWS, -2, 96)

    # Per body rather than per instance, so a look preset can carry the whole glisten - a toon
    # surface and a realistic one differ in all three of these at once.
    glint_gloss = g.cpd_scalar(mat, 'GlintGloss', 380.0, CPD_GLINT_GLOSS, 'Surface', -2, 97,
                               'How tight the sun lobe is. Low is a wide sheen, high is hard sparkle '
                               'over open water.')
    glint_strength = g.cpd_scalar(mat, 'GlintStrength', 2.5, CPD_GLINT_STRENGTH, 'Surface', -2, 98,
                                  'How bright the sun is on the surface.')
    glint_threshold = g.cpd_scalar(mat, 'GlintThreshold', 0.0, CPD_GLINT_THRESHOLD, 'Surface', -2, 99,
                                   'Cuts the sun lobe into separate glints. 0 leaves it a continuous '
                                   'sheen; raising it scatters the surface with distinct bright '
                                   'marks, which is what a stylized surface shows.')

    glint_density = g.cpd_scalar(mat, 'GlintDensity', 1.0, CPD_GLINT_DENSITY, 'Surface', -2, 100,
                                 'How many of the cut glints actually appear. Lower removes whole '
                                 'glints rather than dimming them, so the ones left are scattered.')
    glint_emissive = g.cpd_scalar(mat, 'GlintEmissive', 1.0, CPD_GLINT_EMISSIVE, 'Surface', -2, 101,
                                  'Pushes a glint past what the light could account for, so it blooms '
                                  'and reads as a spark. Above 1 it is no longer a reflection.')

    glint = g.custom(mat, _CODE_GLINT, g.CMOT.CMOT_FLOAT3,
                     ['Normal', 'ViewDirection', 'SunDirection', 'SunColor', 'Gloss', 'Strength',
                      'Threshold', 'Break', 'Density', 'Emissive'],
                     [], -1, 95, 'The sun, added rather than reflected, and optionally cut into '
                                 'separate glints.', includes=INCLUDES)
    g.link(final_normal, '', glint, 'Normal')
    g.link(view_dir, '', glint, 'ViewDirection')
    g.link(sun_direction, '', glint, 'SunDirection')
    g.link(sun_color, '', glint, 'SunColor')
    g.link(glint_gloss, '', glint, 'Gloss')
    g.link(glint_strength, '', glint, 'Strength')
    g.link(glint_threshold, '', glint, 'Threshold')

    # The foam noise, reused. It is already sampled, it is already the right frequency, and a second
    # texture read to scatter some highlights would be a sampler spent on nothing new.
    g.link(foam_noise, 'R', glint, 'Break')
    g.link(glint_density, '', glint, 'Density')
    g.link(glint_emissive, '', glint, 'Emissive')

    # Killed where the water is excluded, so a carved hull does not glint at the sun through a hole
    # in its own deck.
    glint_kept = g.mul(mat, glint, '', kept, '', 0, 95)
    out_glint = g.static_switch(mat, b_exclusion, glint_kept, '', glint, '', 1, 95)

    # --- the sky in it ------------------------------------------------------
    #
    # A reflection is the other half of what makes water read as water. The sun gives it a highlight;
    # this gives it everything else, and Fresnel is what puts almost all of it at grazing angles.
    reflection_params = g.collection_param(mat, collection, 'ReflectionParams', -3, 100)

    reflection_vector = g.expr(mat, unreal.MaterialExpressionReflectionVectorWS, -3, 101)

    reflection_uv = g.custom(mat, _CODE_REFLECTION_UV, g.CMOT.CMOT_FLOAT2,
                             ['ReflectionVector', 'Params'], [], -2, 101,
                             'Where the reflected ray lands on the sky.', includes=INCLUDES)
    g.link(reflection_vector, '', reflection_uv, 'ReflectionVector')
    g.link(reflection_params, '', reflection_uv, 'Params')

    sky_texture = g.existing(g.TEX_ROOT + '/T_MobWaterSky')

    sky = g.texture_param(mat, 'ReflectionTexture', sky_texture, 'Reflection', -1, 101,
                          g.ST.SAMPLERTYPE_COLOR, reflection_uv, '',
                          'The sky this water reflects, as a long-latitude image. A project points '
                          'this at whatever its own backdrop uses.')

    reflection_strength = g.cpd_scalar(mat, 'ReflectionStrength', 1.0, CPD_REFLECTION_STRENGTH,
                                       'Reflection', -1, 103,
                                       'How much sky this body reflects. 0 is none, which is what a '
                                       'flat stylized surface wants - a reflection is a gradient, and '
                                       'a gradient is the thing toon shading is trying not to have.')

    reflection = g.custom(mat, _CODE_REFLECTION, g.CMOT.CMOT_FLOAT3,
                          ['Sky', 'Normal', 'ViewDirection', 'Params', 'BodyStrength'], [], 0, 101,
                          'The sky, weighted by how obliquely the surface is being seen.',
                          includes=INCLUDES)
    g.link(sky, '', reflection, 'Sky')
    g.link(final_normal, '', reflection, 'Normal')
    g.link(view_dir, '', reflection, 'ViewDirection')
    g.link(reflection_params, '', reflection, 'Params')
    g.link(reflection_strength, '', reflection, 'BodyStrength')

    b_reflection = g.static_bool(mat, 'bReflection', True, 'Reflection', 0, 103, 25,
                                 'Reflects a sky texture. Off, the sample and its maths leave the '
                                 'shader and the water has only its own colour and the sun.')

    lit = g.add(mat, out_glint, '', reflection, '', 1, 100)
    out_emissive = g.static_switch(mat, b_reflection, lit, '', out_glint, '', 2, 100)

    # Neither the sun nor the sky reaches the water under solid foam.
    out_emissive = g.mul(mat, out_emissive, '', water_share, '', 2, 102)

    # --- outputs ------------------------------------------------------------
    #
    # The normal is worked out in world space, because that is the space the waves live in, and
    # handed over in tangent space, because that is what the refraction mode requires. On a level
    # plane the two coincide; on a pool that has been rotated they do not, and without this the
    # surface would light correctly until someone turned it.
    tangent_normal = g.transform_vector(mat, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                                        unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT,
                                        final_normal, '', 1, 58)

    # Flattened where the foam hides the water. The ripples the water carries are the last thing
    # shading the foam, and a lit bump under a white band is the pattern that makes foam read as a
    # texture rather than as something floating on the surface.
    #
    # By what the foam hides rather than by the foam itself, so the texture amount winds this back
    # with everything else - flattening here regardless is what left the foam unshaded at 1 however
    # much of the water was being let through.
    tangent_normal = g.lerp(mat, tangent_normal, '', g.const3(mat, (0.0, 0.0, 1.0), 1, 60), '',
                            hidden, '', 2, 59)
    tangent_normal = g.static_switch(mat, b_foam, tangent_normal, '',
                                     g.transform_vector(mat,
                                                        unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                                                        unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT,
                                                        final_normal, '', 1, 61),
                                     '', 3, 59)

    # --- debug views --------------------------------------------------------
    debug_mode = g.scalar_param(mat, 'DebugMode', 1.0, 'Debug', 1, 106,
                                '1 water column, 2 foam, 3 normal, 4 opacity, 5 shore fade, '
                                '6 caustics, 7 glint, 8 reflection, 9 wave fold.')

    debug = g.custom(mat, _CODE_DEBUG, g.CMOT.CMOT_FLOAT3,
                     ['Mode', 'Column', 'Foam', 'Normal', 'Opacity', 'ShoreFade', 'Caustics',
                      'Glint', 'Reflection', 'Fold'], [], 2, 106,
                     'One term of the surface at a time.', includes=INCLUDES)
    g.link(debug_mode, '', debug, 'Mode')
    g.link(column, '', debug, 'Column')
    g.link(foam, '', debug, 'Foam')
    g.link(final_normal, '', debug, 'Normal')
    g.link(out_opacity, '', debug, 'Opacity')
    g.link(shore, '', debug, 'ShoreFade')
    g.link(caustics, '', debug, 'Caustics')
    g.link(out_glint, '', debug, 'Glint')
    g.link(reflection, '', debug, 'Reflection')
    g.link(fold, '', debug, 'Fold')

    b_debug = g.static_bool(mat, 'bDebugView', False, 'Debug', 2, 108, 90,
                            'Replaces the surface with one of the values it is built from. Off, none '
                            'of this is compiled.')

    black = g.const3(mat, (0.0, 0.0, 0.0), 2, 109)
    one = g.const(mat, 1.0, 2, 110)

    unlit = g.cpd_scalar(mat, 'Unlit', 0.0, CPD_UNLIT, 'Colour', 1, 44,
                         'How much of the colour is emitted rather than lit. 1 is a flat colour with '
                         'no ambient tint and no dark side.')

    lit_share = g.sub(mat, g.const(mat, 1.0, 1, 43), '', unlit, '', 2, 43)

    lit_color = g.mul(mat, out_color, '', lit_share, '', 2, 44)
    emitted_color = g.mul(mat, out_color, '', unlit, '', 2, 46)

    out_emissive = g.add(mat, out_emissive, '', emitted_color, '', 2, 99)

    final_color = g.static_switch(mat, b_debug, black, '', lit_color, '', 3, 45)
    final_opacity = g.static_switch(mat, b_debug, one, '', out_opacity, '', 3, 50)
    final_emissive = g.static_switch(mat, b_debug, debug, '', out_emissive, '', 3, 100)

    g.link_property(mat, wpo, '', g.MP.MP_WORLD_POSITION_OFFSET)
    g.link_property(mat, final_color, '', g.MP.MP_BASE_COLOR)
    g.link_property(mat, final_opacity, '', g.MP.MP_OPACITY)
    g.link_property(mat, out_roughness, '', g.MP.MP_ROUGHNESS)
    g.link_property(mat, tangent_normal, '', g.MP.MP_NORMAL)
    g.link_property(mat, out_refraction, '', g.MP.MP_REFRACTION)
    g.link_property(mat, final_emissive, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


# ---------------------------------------------------------------------------
# Wave presets
# ---------------------------------------------------------------------------

WAVE_ROOT = g.ROOT + '/Waves'

# (name, amplitude scale, speed scale, choppiness scale, [(dir_x, dir_y, wavelength, amplitude,
# steepness, phase)])
#
# Three seas rather than one scaled three ways. A pond is not a small ocean: it has ripples and no
# swell, because nothing in it has the fetch to build one. Getting that wrong is the single loudest
# way for enclosed water to read as wrong, and no amount of tuning the amplitude fixes it.
WAVE_PRESETS = [
    ('WP_MobWater_Pond', 1.0, 1.0, 0.25, [
        (1.00, 0.10, 260.0, 1.6, 0.20, 0.0),
        (-0.30, 1.00, 170.0, 0.9, 0.15, 2.1),
    ]),
    ('WP_MobWater_Lake', 1.0, 1.0, 0.5, [
        (1.00, 0.20, 900.0, 7.0, 0.35, 0.0),
        (0.70, -0.70, 520.0, 3.5, 0.30, 1.7),
        (-0.20, 1.00, 310.0, 1.8, 0.25, 3.9),
    ]),
    ('WP_MobWater_Ocean', 1.0, 1.0, 1.0, [
        (1.00, 0.00, 4000.0, 85.0, 0.65, 0.0),
        (0.85, 0.50, 2300.0, 48.0, 0.60, 1.1),
        (0.60, -0.80, 1400.0, 26.0, 0.55, 2.6),
        (-0.30, 0.95, 800.0, 13.0, 0.45, 4.2),
        (0.10, -1.00, 450.0, 6.5, 0.35, 5.4),
    ]),
]


def _vec2f(x, y):
    """FVector2f's Python binding takes no constructor arguments."""
    v = unreal.Vector2f()
    v.set_editor_property('x', float(x))
    v.set_editor_property('y', float(y))
    return v


def build_wave_presets():
    """The seas a body of water starts on.

    Tracked rather than gitignored, unlike the materials: the settings name these by soft path, so a
    clone without them has dangling references, where a clone without a material only has water that
    has not been generated yet.
    """
    built = []

    for name, amplitude, speed, choppiness, waves in WAVE_PRESETS:
        path = WAVE_ROOT + '/' + name

        asset = g.existing(path)
        if asset is None:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.MobWaterWavePreset)
            asset = g.tools().create_asset(name, WAVE_ROOT, unreal.MobWaterWavePreset, factory)

        entries = []
        for dir_x, dir_y, wavelength, wave_amplitude, steepness, phase in waves:
            wave = unreal.MobGerstnerWave()
            wave.set_editor_property('direction', _vec2f(dir_x, dir_y))
            wave.set_editor_property('wavelength', wavelength)
            wave.set_editor_property('amplitude', wave_amplitude)
            wave.set_editor_property('steepness', steepness)
            wave.set_editor_property('phase_offset', phase)
            entries.append(wave)

        params = unreal.MobWaterWaveParams()
        params.set_editor_property('waves', entries)
        params.set_editor_property('amplitude_scale', amplitude)
        params.set_editor_property('speed_scale', speed)
        params.set_editor_property('choppiness_scale', choppiness)

        asset.set_editor_property('waves', params)
        g.save(asset)

        built.append('%s (%d waves)' % (name, len(entries)))

    return built


MESH_NAMES = ['SM_MobWaterPlane', 'SM_MobWaterDisc', 'SM_MobWaterOceanRing']


LOOK_ROOT = g.ROOT + '/Looks'

# The two art directions, as whole settlements rather than two switches.
#
# Stylized reads by shape: a shallow colour that stays readable, a hard shoreline, and almost no
# detail normal, because high frequency detail is what makes a surface read as photographed. Realistic
# reads by depth: a long absorption, foam only where water actually breaks, and the detail normal
# doing most of the work.
#
# Every value is per-body data, so both looks run on the same shaders.
LOOK_PRESETS = [
    ('WL_MobWater_Stylized', {
        'foam_band_separation': 0.0,
        'unlit': 0.0,
        'glint_density': 0.55,
        'glint_emissive': 3.0,
        'reflection_strength': 1.0,
        'min_opacity': 0.0,
        'detail_scroll_speed': 0.6,
        'macro_strength': 1.4,
        'edge_foam_width': 0.10,
        # Absorption, with the ramp waiting on the row that matches it. Both looks were tuned against
        # the two colours, so a preset that arrived already switched would be a look nobody chose;
        # ticking Gradient Color on a body is the whole change, and it lands on the same palette.
        'gradient_color': False,
        'gradient_row': 0,
        # Bright and saturated, and shallow water that is almost not there. The read of stylized
        # water is that you can see the bed through it and the colour is in the depth, not on the
        # surface - so the shallow colour is nearly the deep one, and clarity does the work.
        'shallow_color': (0.20, 0.85, 0.80, 1.0),
        'deep_color': (0.02, 0.35, 0.70, 1.0),
        'fade_depth': 260.0,
        # Long, so the bed stays visible well out from the bank. This is the single value that
        # separates the references from something that looks like painted glass.
        'clarity_depth': 900.0,
        'roughness': 0.04,
        'detail_strength': 0.35,
        'foam': True,
        'foam_opacity': 1.0,
        # A line, not a region. Anything approaching the depth of the water covers all of it.
        'shore_foam_depth': 22.0,
        'crest_foam_threshold': 0.45,
        # Hard edged, and not banded. Quantising foam only works once it is already a narrow band -
        # applied to a region it turns the noise into mottled blotches, which is worse than the
        # gradient it replaced. Bands belong to the toon look, where the band really is a line.
        #
        # The contour follows the shoreline exactly. A wandering edge is what reads as texture on a
        # stylized surface, whatever the foam itself is doing.
        'foam_noise_amount': 0.0,
        'foam_texture': False,
        'foam_texture_opacity': 0.0,
        'foam_sharpness': 8.0,
        'foam_bands': 0.0,
        # On. Seeing the bottom bend is most of why shallow stylized water reads as liquid.
        'caustics': True,
        # Bright and shallow-focused. In the references the caustic is a major read, not a subtlety.
        'caustic_strength': 0.55,
        'caustic_depth': 320.0,
        'refraction': True,
        'refraction_strength': 0.45,
        'ripples': True,
        'ripple_strength': 1.3,
        # Distinct but not cut hard: stylized water sparkles, toon water has marks stamped on it.
        'glint_gloss': 300.0,
        'glint_strength': 3.5,
        'glint_threshold': 0.18,
        'waves': 'WP_MobWater_Pond',
    }),
    # Tuned against a pool rather than derived, so the values here are the ones that were looked at.
    # Several read as compromises on paper and are not: the surface keeps a trace of detail and half
    # its lighting, because fully flat and fully emitted lost the water's shape entirely.
    ('WL_MobWater_Toon', {
        # The look the ramp exists for: the Toon row is three colours with nothing between them,
        # which is a band an absorption cannot produce at any setting.
        'gradient_color': False,
        'gradient_row': 1,
        'shallow_color': (0.06, 0.26, 0.55, 1.0),
        'deep_color': (0.03, 0.14, 0.38, 1.0),
        'fade_depth': 90.0,
        'clarity_depth': 65.0,
        'min_opacity': 0.95,
        'roughness': 0.25,
        # Half emitted. All of it flattens the body into a decal; none of it lets a skylight that
        # captured a field turn blue water green.
        'unlit': 0.5,
        # Present but almost nothing. Zero is flatter than the reference, which keeps the faintest
        # movement across the surface.
        'detail_strength': 0.0001,
        'detail_scroll_speed': 0.02,
        'macro_strength': 2.0,
        # No sky. A Fresnel reflection is a smooth ramp across the whole body and reads as varnish.
        'reflection_strength': 0.0,
        'foam': True,
        'foam_opacity': 1.0,
        'shore_foam_depth': 55.0,
        'crest_foam_threshold': 0.35,
        'foam_noise_amount': 0.4,
        'foam_texture': False,
        'foam_texture_opacity': 0.0,
        'foam_sharpness': 2.5,
        # One band cut to a quarter of its width. More bands crowd the shoreline; the separation is
        # what gives the single line its gap.
        'foam_bands': 1.0,
        'foam_band_separation': 0.75,
        'edge_foam_width': 0.25,
        'caustics': True,
        'caustic_strength': 0.05,
        'caustic_depth': 200.0,
        'refraction': False,
        'refraction_strength': 0.3,
        'ripples': True,
        'ripple_strength': 1.0,
        'glint_gloss': 160.0,
        'glint_strength': 2.5,
        'glint_threshold': 0.08,
        'glint_density': 1.0,
        'glint_emissive': 1.0,
        'waves': 'WP_MobWater_Pond',
    }),
    ('WL_MobWater_Realistic', {
        # Absorption, and not a candidate for the ramp. Water losing red first and blue last is what
        # the exponential is; a painted ramp would have to reproduce it stop by stop to break even.
        'gradient_color': False,
        'gradient_row': 0,
        'foam_band_separation': 0.0,
        'unlit': 0.0,
        'glint_density': 1.0,
        'glint_emissive': 1.0,
        'reflection_strength': 1.0,
        'min_opacity': 0.0,
        'detail_scroll_speed': 1.0,
        'macro_strength': 2.2,
        'edge_foam_width': 0.10,
        'shallow_color': (0.10, 0.30, 0.32, 1.0),
        'deep_color': (0.005, 0.04, 0.08, 1.0),
        'fade_depth': 420.0,
        'clarity_depth': 700.0,
        # Nearly a mirror. Water is one of the smoothest things in any scene and reading it as
        # anything else is the fastest way to lose it.
        'roughness': 0.015,
        'detail_strength': 1.0,
        'foam': True,
        'foam_opacity': 1.0,
        'shore_foam_depth': 18.0,
        'crest_foam_threshold': 0.6,
        # Soft and continuous. Real foam has no edge, it has a wet margin.
        'foam_noise_amount': 0.55,
        'foam_texture': True,
        'foam_texture_opacity': 0.6,
        'foam_sharpness': 2.0,
        'foam_bands': 0.0,
        'caustics': True,
        'caustic_strength': 0.3,
        'caustic_depth': 500.0,
        'refraction': True,
        'refraction_strength': 0.35,
        'ripples': True,
        'ripple_strength': 1.0,
        # A continuous sheen, because that is what real water shows.
        'glint_gloss': 420.0,
        'glint_strength': 2.5,
        'glint_threshold': 0.0,
        'waves': 'WP_MobWater_Lake',
    }),
]


def build_look_presets():
    """The two art directions a project starts from."""
    built = []

    for name, values in LOOK_PRESETS:
        path = LOOK_ROOT + '/' + name

        asset = g.existing(path)
        if asset is None:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.MobWaterLookPreset)
            asset = g.tools().create_asset(name, LOOK_ROOT, unreal.MobWaterLookPreset, factory)

        asset.set_editor_property('gradient_color', values['gradient_color'])
        asset.set_editor_property('gradient_row', values['gradient_row'])
        asset.set_editor_property('shallow_color', unreal.LinearColor(*values['shallow_color']))
        asset.set_editor_property('deep_color', unreal.LinearColor(*values['deep_color']))
        asset.set_editor_property('fade_depth', values['fade_depth'])
        asset.set_editor_property('clarity_depth', values['clarity_depth'])
        asset.set_editor_property('min_opacity', values['min_opacity'])
        asset.set_editor_property('unlit', values['unlit'])
        asset.set_editor_property('roughness', values['roughness'])
        asset.set_editor_property('detail_strength', values['detail_strength'])
        asset.set_editor_property('detail_scroll_speed', values['detail_scroll_speed'])
        asset.set_editor_property('macro_strength', values['macro_strength'])
        asset.set_editor_property('edge_foam_width', values['edge_foam_width'])
        asset.set_editor_property('foam_opacity', values['foam_opacity'])
        asset.set_editor_property('foam', values['foam'])
        asset.set_editor_property('shore_foam_depth', values['shore_foam_depth'])
        asset.set_editor_property('crest_foam_threshold', values['crest_foam_threshold'])
        asset.set_editor_property('foam_noise_amount', values['foam_noise_amount'])
        asset.set_editor_property('foam_texture', values['foam_texture'])
        asset.set_editor_property('foam_texture_opacity', values['foam_texture_opacity'])
        asset.set_editor_property('foam_sharpness', values['foam_sharpness'])
        asset.set_editor_property('foam_bands', values['foam_bands'])
        asset.set_editor_property('foam_band_separation', values['foam_band_separation'])
        asset.set_editor_property('glint_gloss', values['glint_gloss'])
        asset.set_editor_property('glint_strength', values['glint_strength'])
        asset.set_editor_property('glint_threshold', values['glint_threshold'])
        asset.set_editor_property('glint_density', values['glint_density'])
        asset.set_editor_property('glint_emissive', values['glint_emissive'])
        asset.set_editor_property('caustics', values['caustics'])
        asset.set_editor_property('caustic_strength', values['caustic_strength'])
        asset.set_editor_property('caustic_depth', values['caustic_depth'])
        asset.set_editor_property('refraction', values['refraction'])
        asset.set_editor_property('refraction_strength', values['refraction_strength'])
        asset.set_editor_property('reflection_strength', values['reflection_strength'])
        asset.set_editor_property('ripples', values['ripples'])
        asset.set_editor_property('ripple_strength', values['ripple_strength'])

        waves = g.existing(WAVE_ROOT + '/' + values['waves'])
        if waves is not None:
            asset.set_editor_property('waves', waves)

        g.save(asset)
        built.append(name)

    return built


def build_meshes():
    """The surfaces the bodies rasterise. Built in C++, because a mesh is not a material graph.

    Saved here rather than there: the builder creates the packages and marks them dirty, and a
    commandlet exits without anyone prompting to save, so a mesh built and never written is a mesh
    that silently does not exist next run.
    """
    unreal.MobWaterMeshLibrary.build_surface_meshes()

    built = []
    for name in MESH_NAMES:
        # The loaded mesh, never the path. Saving by path reloads the package to find what to save,
        # and a load that has to wait for a static mesh still compiling ensures inside the engine.
        mesh = g.existing(g.MESH_ROOT + '/' + name)
        if mesh is not None:
            g.save(mesh)
            built.append(name)
        else:
            g.log_error('mesh %s was not built' % name)

    return built


# ---------------------------------------------------------------------------
# Instances
# ---------------------------------------------------------------------------

# (name, bRadialShore, bShoreFromVertex, bSpectrum)
#
# One family per shape, because the shape is a static switch: a rectangle and a disc measure the
# distance to their own bank differently, and a switch resolved at compile time is a branch that
# leaves the shader rather than one paid for on every vertex.
#
# The ocean is a family of its own rather than the disc's, which is what it used to borrow. It is the
# only shape that reads a baked sea state, and a spectrum tap on every pond in the level to serve one
# body that has one is exactly the cost this plugin is arranged around.
SHAPES = [
    ('Box', False, False, False),
    ('Disc', True, False, False),
    ('Spline', False, True, False),
    ('Ocean', False, False, True),
]

# Has to agree with namespace MobWaterVariant in MobWaterTypes.h.
VARIANT_RIPPLES = 1 << 0
VARIANT_FOAM = 1 << 1
VARIANT_REFRACTION = 1 << 2
VARIANT_FOAM_TEXTURE = 1 << 3
VARIANT_GRADIENT = 1 << 4

VARIANT_NUM = 32

# Every combination that can actually be asked for. Foam texture without foam is not one of them, so
# those four are never generated - an instance nothing can select is a shader nobody compiles and a
# row in the settings that only makes the table harder to read.
VARIANTS = [v for v in range(VARIANT_NUM)
            if not (v & VARIANT_FOAM_TEXTURE) or (v & VARIANT_FOAM)]

# The foam's own texture is read in the shoreline's frame, and an ocean has no shoreline - the
# coordinates it would be sampled in are not defined out there. So the ocean carries sixteen
# combinations rather than twenty four, and a body that asks for one it does not have falls to the
# same variant without it, which is what the settings' drop order is for.
OCEAN_VARIANTS = [v for v in VARIANTS if not (v & VARIANT_FOAM_TEXTURE)]


def _variant_suffix(variant):
    """Built in bit order, so the name the generator writes and the name C++ expects agree."""
    out = ''
    if variant & VARIANT_RIPPLES:
        out += '_Ripples'
    if variant & VARIANT_FOAM:
        out += '_Foam'
    if variant & VARIANT_REFRACTION:
        out += '_Refraction'
    if variant & VARIANT_FOAM_TEXTURE:
        out += '_FoamTexture'
    if variant & VARIANT_GRADIENT:
        out += '_Gradient'
    return out


def build_material_instances(master):
    built = []

    for shape, radial, from_vertex, spectrum in SHAPES:
        for variant in (OCEAN_VARIANTS if spectrum else VARIANTS):
            name = 'MI_MobWater_%s%s' % (shape, _variant_suffix(variant))

            instance = g.get_or_create_instance(g.MAT_ROOT, name, master)

            g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bRadialShore', radial)
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bShoreFromVertex', from_vertex)
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bFoam', bool(variant & VARIANT_FOAM))
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bRefraction', bool(variant & VARIANT_REFRACTION))
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bRipples', bool(variant & VARIANT_RIPPLES))
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bFoamTexture', bool(variant & VARIANT_FOAM_TEXTURE))
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bGradientColor', bool(variant & VARIANT_GRADIENT))
            g.MEL.set_material_instance_static_switch_parameter_value(
                instance, 'bSpectrum', spectrum)

            g.MEL.update_material_instance(instance)
            g.save(instance)
            built.append(name)

    # One debug instance per shape. Assign it to a body's material slot to see what the surface is
    # actually made of, then put the real one back.
    for shape, radial, from_vertex, spectrum in SHAPES:
        name = 'MI_MobWater_%s_Debug' % shape

        instance = g.get_or_create_instance(g.MAT_ROOT, name, master)

        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bRadialShore', radial)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bShoreFromVertex', from_vertex)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bDebugView', True)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bFoam', True)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bRipples', True)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bCaustics', True)
        g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bSpectrum', spectrum)

        g.MEL.update_material_instance(instance)
        g.save(instance)
        built.append(name)

    return built


# ---------------------------------------------------------------------------
# Underwater
# ---------------------------------------------------------------------------

UNDERWATER_NAME = 'M_MobWaterUnderwater'

# The underwater plane's own custom primitive data. Has to agree with MobUnderwaterData in
# MobWaterUnderwaterComponent.cpp.
UW_ABSORB_COLOR = 0
UW_CLARITY = 3
UW_SUBMERSION = 4

_CODE_UNDERWATER = """
return MobWaterUnderwaterOpacity(SceneDepth, Clarity, Submersion);
"""


def build_underwater_material():
    """What a camera under the surface looks through.

    Unlit and translucent on a plane held in front of the near clip. Depth testing stays on: the
    plane is nearer than anything in the scene anyway, and turning it off would draw it over the
    editor's own gizmos as well.
    """
    mat = g.get_or_create_material(g.MAT_ROOT, UNDERWATER_NAME)

    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('two_sided', True)

    scene_depth = g.expr(mat, unreal.MaterialExpressionSceneDepth, -3, 0)

    clarity = g.cpd_scalar(mat, 'Clarity', 1200.0, UW_CLARITY, 'Underwater', -3, 2,
                           'How far light travels through the water before it is gone.')
    submersion = g.cpd_scalar(mat, 'Submersion', 1.0, UW_SUBMERSION, 'Underwater', -3, 3,
                              'Fades the whole effect in across the waterline.')

    absorb = g.cpd_vector(mat, 'AbsorbColor', (0.02, 0.09, 0.13, 1.0), UW_ABSORB_COLOR,
                          'Underwater', -3, 4, 'What the water absorbs down to.')

    opacity = g.custom(mat, _CODE_UNDERWATER, g.CMOT.CMOT_FLOAT1,
                       ['SceneDepth', 'Clarity', 'Submersion'], [], -1, 1,
                       'How much of what is behind this pixel the water has taken away.',
                       includes=INCLUDES)
    g.link(scene_depth, '', opacity, 'SceneDepth')
    g.link(clarity, '', opacity, 'Clarity')
    g.link(submersion, '', opacity, 'Submersion')

    g.link_property(mat, absorb, '', g.MP.MP_EMISSIVE_COLOR)
    g.link_property(mat, opacity, '', g.MP.MP_OPACITY)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


# ---------------------------------------------------------------------------
# The parity probe
# ---------------------------------------------------------------------------

PARITY_NAME = 'M_MobWaterParity'

# Displacement is signed and an emissive output is not, so it is biased into the positive half before
# it is written. The render target is 32 bit float, so this costs nothing in precision - it exists
# purely because emissive cannot carry a negative number.
PARITY_ENCODE_SCALE = 1000.0

_CODE_PARITY_UV = """
return Origin + (UV - 0.5f) * Extent;
"""

_CODE_PARITY = """
float3 Disp = float3(0.0f, 0.0f, 0.0f);
float3 Nrm = float3(0.0f, 0.0f, 1.0f);
float Fold = 0.0f;

MobWaterEvaluate(
	A0, B0, A1, B1, A2, B2, A3, B3,
	A4, B4, A5, B5, A6, B6, A7, B7,
	SampleXY, Time,
	Scales.y, Scales.z, Scales.w, Scales.x,
	Disp, Nrm, Fold);

return Disp / EncodeScale + 0.5f;
"""


def build_parity_probe():
    """A material that evaluates the wave set and writes the answer out as pixels.

    The whole point of it is that mob_water_verify can then compare those pixels against
    FMobWaterWaves::Evaluate and fail when the shader and the header have parted. Nothing else keeps
    them equal.

    Its wave set arrives as its own parameters rather than through the collection, deliberately. A
    probe reading the collection would agree with the CPU perfectly in a world where nothing has
    published anything - both would evaluate an empty set and both would answer zero - and the test
    would pass without having compared anything.
    """
    mat = g.get_or_create_material(g.MAT_ROOT, PARITY_NAME)

    # UI domain, because that is the domain the canvas draws with. A surface material handed to
    # DrawMaterialToRenderTarget produces a target that is untouched black, with no warning anywhere -
    # which reads as the wave maths disagreeing rather than as nothing having been drawn.
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -4, 0)

    origin = g.vector_param4(mat, 'ProbeOrigin', (0.0, 0.0, 0.0, 0.0), 'Probe', -5, 2)

    origin_xy = g.expr(mat, unreal.MaterialExpressionComponentMask, -3, 2)
    origin_xy.set_editor_property('r', True)
    origin_xy.set_editor_property('g', True)
    origin_xy.set_editor_property('b', False)
    origin_xy.set_editor_property('a', False)
    g.link(origin, '', origin_xy, '')

    extent = g.scalar_param(mat, 'ProbeExtent', 4000.0, 'Probe', -3, 4,
                            'How much world the probe covers, corner to corner.')

    sample_xy = g.custom(mat, _CODE_PARITY_UV, g.CMOT.CMOT_FLOAT2, ['UV', 'Origin', 'Extent'], [],
                         -2, 1, 'Where in the world each texel stands for.', includes=INCLUDES)
    g.link(uv, '', sample_xy, 'UV')
    g.link(origin_xy, '', sample_xy, 'Origin')
    g.link(extent, '', sample_xy, 'Extent')

    inputs = []
    sources = []

    for i in range(MAX_WAVES):
        for slot in ('A', 'B'):
            sources.append(g.vector_param4(mat, 'Wave%s%d' % (slot, i), (0.0, 0.0, 100.0, 0.0),
                                           'Probe', -5, 8 + i * 2))
            inputs.append('%s%d' % (slot, i))

    scales = g.vector_param4(mat, 'WaveScales', (0.0, 1.0, 1.0, 1.0), 'Probe', -5, 6)

    time = g.scalar_param(mat, 'Time', 0.0, 'Probe', -4, 5, 'The instant to evaluate at.')
    encode = g.scalar_param(mat, 'EncodeScale', PARITY_ENCODE_SCALE, 'Probe', -4, 4, '')

    inputs += ['SampleXY', 'Time', 'Scales', 'EncodeScale']

    node = g.custom(mat, _CODE_PARITY, g.CMOT.CMOT_FLOAT3, inputs, [], -1, 8,
                    'The wave set, as the GPU sees it.', includes=INCLUDES)

    for name, src in zip(inputs, sources + [sample_xy, time, scales, encode]):
        g.link(src, '', node, name)

    g.link_property(mat, node, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


SPECTRUM_PARITY_NAME = 'M_MobWaterSpectrumParity'

_CODE_SPECTRUM_PARITY = """
float Blend = 0.0f;
float4 UV = MobWaterSpectrumUV(SampleXY, Time, Params, Scale.w, Blend);

float Fold = 0.0f;
float3 Disp = MobWaterSpectrumDisplacement(S0, S1, Blend, float3(Scale.x, Scale.x, Scale.y), Fold);

return Disp / EncodeScale + 0.5f;
"""


def build_spectrum_parity_probe():
    """A material that reads the baked sea and writes what it read out as pixels.

    The Gerstner probe compares two implementations of one piece of arithmetic and expects them to
    agree to a part in a million. This one cannot: it compares a hardware bilinear filter against a
    software one, and a filter's subtexel weights are fixed point on every GPU there is. What it is
    for is measuring what that costs rather than assuming it, and catching the failures that are not
    rounding at all - an atlas addressed at the wrong scale, a frame index off by one, a decode that
    lost its bias, a gutter that is not there.

    Its layout arrives as its own parameters rather than through the collection, for the same reason
    the wave probe's set does: a probe reading an unpublished collection would agree with a CPU
    reading an unbaked asset, and the test would pass without having compared anything.
    """
    mat = g.get_or_create_material(g.MAT_ROOT, SPECTRUM_PARITY_NAME)

    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -6, 0)

    origin = g.vector_param4(mat, 'ProbeOrigin', (0.0, 0.0, 0.0, 0.0), 'Probe', -7, 2)

    origin_xy = g.expr(mat, unreal.MaterialExpressionComponentMask, -5, 2)
    origin_xy.set_editor_property('r', True)
    origin_xy.set_editor_property('g', True)
    origin_xy.set_editor_property('b', False)
    origin_xy.set_editor_property('a', False)
    g.link(origin, '', origin_xy, '')

    extent = g.scalar_param(mat, 'ProbeExtent', 4000.0, 'Probe', -5, 4,
                            'How much world the probe covers, corner to corner.')

    sample_xy = g.custom(mat, _CODE_PARITY_UV, g.CMOT.CMOT_FLOAT2, ['UV', 'Origin', 'Extent'], [],
                         -4, 1, 'Where in the world each texel stands for.', includes=INCLUDES)
    g.link(uv, '', sample_xy, 'UV')
    g.link(origin_xy, '', sample_xy, 'Origin')
    g.link(extent, '', sample_xy, 'Extent')

    params = g.vector_param4(mat, 'SpectrumParams', (1024.0, 1.0, 4.0, 2.0), 'Probe', -7, 6)
    scale = g.vector_param4(mat, 'SpectrumScale', (0.0, 0.0, 0.0, 1.0), 'Probe', -7, 8)
    time = g.scalar_param(mat, 'Time', 0.0, 'Probe', -5, 6, 'The instant to evaluate at.')
    encode = g.scalar_param(mat, 'EncodeScale', PARITY_ENCODE_SCALE, 'Probe', -5, 8, '')

    coords = g.custom(mat, _CODE_SPECTRUM_UV, g.CMOT.CMOT_FLOAT4,
                      ['WorldXY', 'Time', 'Params', 'Scale'],
                      [('SpectrumBlend', g.CMOT.CMOT_FLOAT1)], -3, 4,
                      'Where in the atlas each texel is.', includes=INCLUDES)
    g.link(sample_xy, '', coords, 'WorldXY')
    g.link(time, '', coords, 'Time')
    g.link(params, '', coords, 'Params')
    g.link(scale, '', coords, 'Scale')

    atlas = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -3, 8)
    atlas.set_editor_property('parameter_name', 'SpectrumDisplacement')
    atlas.set_editor_property('texture', g.existing(SPECTRUM_ROOT + '/T_MobWaterSpectrum'))
    atlas.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    atlas.set_editor_property('group', 'Probe')

    taps = []
    for index in range(2):
        mask = g.expr(mat, unreal.MaterialExpressionComponentMask, -2, 4 + index)
        mask.set_editor_property('r', index == 0)
        mask.set_editor_property('g', index == 0)
        mask.set_editor_property('b', index == 1)
        mask.set_editor_property('a', index == 1)
        g.link(coords, '', mask, '')

        taps.append(_atlas_tap(mat, atlas, mask, -1, 4 + index * 2))

    node = g.custom(mat, _CODE_SPECTRUM_PARITY, g.CMOT.CMOT_FLOAT3,
                    ['SampleXY', 'Time', 'Params', 'Scale', 'S0', 'S1', 'EncodeScale'], [],
                    0, 6, 'The baked sea, as the GPU reads it.', includes=INCLUDES)

    for name, src in zip(['SampleXY', 'Time', 'Params', 'Scale', 'S0', 'S1', 'EncodeScale'],
                         [sample_xy, time, params, scale, taps[0], taps[1], encode]):
        g.link(src, '', node, name)

    g.link_property(mat, node, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_all():
    # Reloaded here rather than left to the caller: mob_water_graph holds the reference, so a stamp
    # edited during a session would otherwise keep writing the version it was imported with.
    importlib.reload(mob_water_version)

    g.log('Authoring water %s' % mob_water_version.plugin_version())

    # Before the master: it names these as parameter defaults, and a texture parameter whose default
    # is missing falls back to the engine's grey, which is not a normal and lights like a dent.
    importlib.reload(mob_water_textures)
    mob_water_textures.build_all()

    for mesh in build_meshes():
        g.log('  mesh %s' % mesh)

    # Before the master, which names the field's render target as a parameter default.
    importlib.reload(author_ripples)
    for target in author_ripples.build_all():
        g.log('  field %s' % target)

    collection = build_parameter_collection()
    g.log('  collection %s' % collection.get_path_name())

    for preset in build_wave_presets():
        g.log('  waves %s' % preset)

    # After the wave presets, which they point at.
    for look in build_look_presets():
        g.log('  look %s' % look)

    # Before the master, which names the baked atlas as a parameter default and the asset itself on
    # the coordinate node.
    for row in build_gradients():
        g.log('  gradient %s' % row)

    # Before the master, which names the two atlases as parameter defaults. Only when there is not
    # one already: the transform is minutes of arithmetic and nothing about generating materials
    # changes the sea. Water > Bake Ocean Spectrum is how a bake is asked for on purpose.
    importlib.reload(mob_water_spectrum)
    if g.existing(mob_water_spectrum.SPECTRUM_ROOT + '/' + mob_water_spectrum.ASSET_NAME) is None:
        g.log('  no baked sea state; solving one')
        mob_water_spectrum.build()

    master = build_master_material()
    g.log('  master %s' % master.get_path_name())

    for instance in build_material_instances(master):
        g.log('  instance %s' % instance)

    underwater = build_underwater_material()
    g.log('  underwater %s' % underwater.get_path_name())

    probe = build_parity_probe()
    g.log('  probe %s' % probe.get_path_name())

    sea_probe = build_spectrum_parity_probe()
    g.log('  probe %s' % sea_probe.get_path_name())

    # Vertex as well as pixel, because this material's own cost is mostly vertex: the waves are
    # evaluated there, and the pixel count is dominated by what a translucent lit surface costs
    # before anything is written into it. Reporting only the pixel figure hides where the work is.
    #
    # Both are worst case. The shader unrolls all eight wave slots and skips the unused ones at run
    # time, so a pond running two waves executes a fraction of what is counted here.
    stats = g.MEL.get_statistics(master)
    g.log('  %d vertex, %d pixel instructions, %d samplers, %d interpolator scalars'
          % (stats.get_editor_property('num_vertex_shader_instructions'),
             stats.get_editor_property('num_pixel_shader_instructions'),
             stats.get_editor_property('num_samplers'),
             stats.get_editor_property('num_interpolator_scalars')))

    g.log('Done.')
    return True
