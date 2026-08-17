# Copyright (c) Jared Taylor. All Rights Reserved

"""Authors the ripple field: its two render targets, the material that steps it, the one that copies
it back, and the one that adds whatever is standing in it.

The field is a wave equation on a grid, not a decal that fades. Red carries the height now, green
carries the height a step ago, and one step of

    h' = (left + right + down + up) * 0.5 - previous

is what makes a ripple spread outwards, reflect off the edge of the field, and interfere with the
next one rather than simply sitting where it was stamped.

Two targets because a step has to read the frame before it while it is writing this one. Reading and
writing the same target gives each texel a different mixture of old and new depending on where the
rasteriser got to, which looks like noise crawling across the water.
"""

import unreal

import mob_water_graph as g

STEP_NAME = 'M_MobWaterRippleStep'
COPY_NAME = 'M_MobWaterRippleCopy'
STAMP_NAME = 'M_MobWaterRippleStamp'

# How many pushes one step can carry. Sorted strongest first by the subsystem, so an overflow drops
# the ones nobody would have seen. Eight is a character, its wake and a few impacts at once.
STAMP_SLOTS = 8

# How many baked mesh outlines one step can carry into the field's alpha. Has to agree with
# MOB_WATER_MESH_EXCLUSION_SLOTS in MobWaterExclusionComponent.h.
MESH_SLOTS = 4

EXCLUSION_NAME = 'M_MobWaterExclusionField'

TARGET_NAME = 'RT_MobWaterRipple'
HISTORY_NAME = 'RT_MobWaterRippleHistory'
EXCLUSION_TARGET_NAME = 'RT_MobWaterExclusion'

# Sixteen bit float rather than eight bit. The height is signed and small, and a wave equation reads
# its own output every step - eight bit quantisation compounds into visible terracing within a
# second, and then never settles because the quantisation itself keeps feeding it.
FIELD_SIZE = 256
FIELD_FORMAT = unreal.TextureRenderTargetFormat.RTF_RGBA16F

# Eight bits a channel for the outlines. It is a mask read once, not a wave equation reading itself.
EXCLUSION_FORMAT = unreal.TextureRenderTargetFormat.RTF_RGBA8

INCLUDES = ['/MobWater/Public/MobWaterField.ush']

_CODE_STEP = """
return MobWaterRippleStep(History, HistorySampler, UV, Scroll.xy, TexelSize, Speed, Damping);
"""

_CODE_EXCLUSION = """
return MobWaterMeshExclusions(MobWaterFieldWorldXY(UV, Area),
	Mask0, Mask0Sampler, MeshA0, MeshB0,
	Mask1, Mask1Sampler, MeshA1, MeshB1,
	Mask2, Mask2Sampler, MeshA2, MeshB2,
	Mask3, Mask3Sampler, MeshA3, MeshB3);
"""

_CODE_COPY = """
return Texture2DSample(Source, SourceSampler, UV);
"""

_CODE_STAMP = """
const float4 Field = Texture2DSample(Source, SourceSampler, UV);
return MobWaterRippleStamp(Field, UV, S0, S1, S2, S3, S4, S5, S6, S7, FoamA, FoamB);
"""


def build_render_targets():
    """The pair the field lives in, and the window the outlines are drawn into."""
    built = []

    for name in (TARGET_NAME, HISTORY_NAME, EXCLUSION_TARGET_NAME):
        path = g.TEX_ROOT + '/' + name

        target = g.existing(path)
        if target is None:
            factory = unreal.TextureRenderTargetFactoryNew()
            target = g.tools().create_asset(name, g.TEX_ROOT, unreal.TextureRenderTarget2D, factory)

        target.set_editor_property('size_x', FIELD_SIZE)
        target.set_editor_property('size_y', FIELD_SIZE)

        # The outlines are a mask, so eight bits a channel is plenty and sixteen would be a quarter
        # of a megabyte spent holding nothing.
        target.set_editor_property(
            'render_target_format',
            EXCLUSION_FORMAT if name == EXCLUSION_TARGET_NAME else FIELD_FORMAT)

        # Clamped, not wrapped. A ripple that reaches the edge of the field and comes back in on the
        # far side is the one artefact of a scrolling field that nobody can explain away.
        target.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        target.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)

        # Flat water, not black. Both heights are stored biased, so the colour a render target holds
        # when its resource is first created is the state the field starts in - and black decodes to
        # the surface pushed as far down as the field can hold. The step drags the border back to
        # flat while the middle is still railed, and that step in the surface travels inward as a
        # square shockwave that takes about a second to pass. It is seen once, on the first water
        # anything looks at, which is the hardest kind of artefact to catch.
        target.set_editor_property(
            'clear_color',
            unreal.LinearColor(0.0, 0.0, 0.0, 1.0) if name == EXCLUSION_TARGET_NAME
            else unreal.LinearColor(0.5, 0.5, 0.0, 1.0))

        g.save(target)
        built.append(name)

    return built


def _ui_material(name):
    """A material the canvas can draw. UI domain, because that is what DrawMaterialToRenderTarget
    uses - a surface material handed to it produces an untouched black target and no warning."""
    mat = g.get_or_create_material(g.MAT_ROOT, name)
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    return mat


def build_step_material():
    mat = _ui_material(STEP_NAME)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -4, 0)

    history = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -4, 2)
    history.set_editor_property('parameter_name', 'History')
    history.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    scroll = g.expr(mat, unreal.MaterialExpressionVectorParameter, -4, 4)
    scroll.set_editor_property('parameter_name', 'Scroll')
    scroll.set_editor_property('default_value', unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    texel = g.scalar_param(mat, 'TexelSize', 1.0 / FIELD_SIZE, 'Field', -4, 5, '')
    speed = g.scalar_param(mat, 'Speed', 0.28, 'Field', -4, 6, '')
    damping = g.scalar_param(mat, 'Damping', 0.985, 'Field', -4, 7, '')

    step = g.custom(mat, _CODE_STEP, g.CMOT.CMOT_FLOAT4,
                    ['History', 'UV', 'Scroll', 'TexelSize', 'Speed', 'Damping'], [], -2, 3,
                    'One step of the wave equation, on a field that is scrolling under it.',
                    includes=INCLUDES)

    g.link(history, '', step, 'History')
    g.link(uv, '', step, 'UV')
    g.link(scroll, '', step, 'Scroll')
    g.link(texel, '', step, 'TexelSize')
    g.link(speed, '', step, 'Speed')
    g.link(damping, '', step, 'Damping')

    g.link_property(mat, step, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_exclusion_material():
    """The baked mesh outlines, drawn into a window that follows the view.

    Its own target rather than the ripple field's spare channel. A UI domain material reaches its
    target through emissive, which is three channels, and whatever the fourth is set to is discarded
    on the way out with nothing said - so the field's alpha reads zero however carefully it is
    written. Its own target also means a still pool keeps its hole: outlines do not depend on the
    body reading ripples.
    """
    mat = _ui_material(EXCLUSION_NAME)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -4, 0)

    # Where the window is in the world, so an outline can be placed in centimetres.
    area = g.vector_param4(mat, 'ExclusionArea', (0.0, 0.0, 2000.0, 0.0005), 'Field', -4, 2,
                           'Where the window is: origin, extent, and one over the extent.')

    names = ['UV', 'Area']
    nodes = [uv, area]

    # One texture and two vectors each. A texture object handed to a Custom node brings its own
    # sampler rather than a shared one, so these are four of this material's sixteen - which is
    # affordable on a 256 square pass and would not be on the surface, where the budget binds.
    mask_default = g.existing(g.TEX_ROOT + '/T_MobWaterCaustics')

    for slot in range(MESH_SLOTS):
        mask = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -4, 4 + slot * 3)
        mask.set_editor_property('parameter_name', 'MeshMask%d' % slot)
        mask.set_editor_property('texture', mask_default)
        mask.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        mask.set_editor_property('group', 'Mesh Exclusion')

        names.append('Mask%d' % slot)
        nodes.append(mask)

        names.append('MeshA%d' % slot)
        nodes.append(g.vector_param4(mat, 'MeshA%d' % slot, (0.0, 0.0, 1.0, 1.0), 'Mesh Exclusion',
                                     -4, 5 + slot * 3))

        names.append('MeshB%d' % slot)
        nodes.append(g.vector_param4(mat, 'MeshB%d' % slot, (0.0, 0.0, 0.0, 0.0), 'Mesh Exclusion',
                                     -4, 6 + slot * 3))

    draw = g.custom(mat, _CODE_EXCLUSION, g.CMOT.CMOT_FLOAT1, names, [], -1, 4,
                    'How much water the nearest four baked outlines keep out of each texel.',
                    includes=INCLUDES)

    for name, node in zip(names, nodes):
        g.link(node, '', draw, name)

    g.link_property(mat, draw, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_copy_material():
    mat = _ui_material(COPY_NAME)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -3, 0)

    source = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -3, 2)
    source.set_editor_property('parameter_name', 'Source')
    source.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    copy = g.custom(mat, _CODE_COPY, g.CMOT.CMOT_FLOAT4, ['Source', 'UV'], [], -1, 1,
                    'The stepped field, put back where the next step will read it.',
                    includes=INCLUDES)
    g.link(source, '', copy, 'Source')
    g.link(uv, '', copy, 'UV')

    g.link_property(mat, copy, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_stamp_material():
    """The copy back into the history, with every push waiting this step added on the way through.

    A material rather than a canvas draw. Canvas stamping and DrawMaterialToRenderTarget are two
    paths to the same target that do not reliably land in the order they were asked for, and the
    stamp is the one that loses: it is put in, the step reads the target without it, and the field
    never receives a ripple while every log along the way reports one stamped.
    """
    mat = _ui_material(STAMP_NAME)

    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -6, 0)

    source = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -6, 2)
    source.set_editor_property('parameter_name', 'Source')
    source.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    names = ['Source', 'UV']
    nodes = [source, uv]

    for name in ['S%d' % slot for slot in range(STAMP_SLOTS)] + ['FoamA', 'FoamB']:
        names.append(name)
        nodes.append(g.vector_param4(mat, name, (0.0, 0.0, 0.0, 0.0), 'Stamps',
                                     -6, 4 + len(nodes)))

    stamp = g.custom(mat, _CODE_STAMP, g.CMOT.CMOT_FLOAT4, names, [], -2, 6,
                     'The stepped field, put back where the next step will read it, plus whatever '
                     'is standing in the water this step.',
                     includes=INCLUDES)

    for name, node in zip(names, nodes):
        g.link(node, '', stamp, name)

    g.link_property(mat, stamp, '', g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_all():
    targets = build_render_targets()
    build_step_material()
    build_copy_material()
    build_stamp_material()
    build_exclusion_material()
    return targets
