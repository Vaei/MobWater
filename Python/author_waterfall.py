# Copyright (c) Jared Taylor. All Rights Reserved

"""Authors the waterfall master material and its instances.

Its own module, and its own master, because a fall is not a shape of the surface. Every meaning the
surface attaches to its own geometry is a plan view - the ripple field is looked down on, the shore
distance is measured across the ground, an exclusion volume is a footprint, and a Gerstner wave moves
an XY vertex in Z. None of those survive being stood on end.

What is shared is what is genuinely shared: the clock, the normal and foam textures, the sky, the
colour ramp and the look preset. A fall that does not match the river it comes out of is worse than
no fall at all.

Called from author_water.build_all, which owns the collection and the gradient this reads.
"""

import unreal

import mob_water_graph as g
import water_hooks

FALL_NAME = 'M_MobWaterFall'

INCLUDES = ['/MobWater/Public/MobWaterFall.ush']

# The contract with MobWaterFallData in MobWaterTypes.h. mob_water_verify asserts the two agree, and
# a disagreement here is a fall that renders with somebody else's numbers in it.
CPD_THIN_COLOR = 0
CPD_GRADIENT_ROW = 0
CPD_THICK_COLOR = 3
CPD_THICKNESS = 6
CPD_CLARITY_DEPTH = 7
CPD_MIN_OPACITY = 8
CPD_UNLIT = 9
CPD_ROUGHNESS = 10
CPD_DETAIL_STRENGTH = 11
CPD_SIZE = 12
CPD_LIP_SPEED = 14
CPD_GRAVITY = 15
CPD_STREAK_SIZE = 16
CPD_THIN_AMOUNT = 18
CPD_BREAKUP = 19
CPD_EDGE_FADE = 20
CPD_FOAM_AMOUNT = 21
CPD_LIP_FOAM = 22
CPD_BASE_FOAM = 23
CPD_GLINT_GLOSS = 24
CPD_GLINT_STRENGTH = 25
CPD_REFLECTION_STRENGTH = 26
CPD_LIP_OFFSET = 27
CPD_LIP_FADE = 29
CPD_REFRACTION_STRENGTH = 30

VARIANT_FOAM = 1 << 0
VARIANT_REFRACTION = 1 << 1
VARIANT_GRADIENT = 1 << 2

VARIANT_NUM = 8


def variant_suffix(variant):
    """The name a combination gets. Bit order, so the generator and the lookup cannot disagree."""
    out = ''
    if variant & VARIANT_FOAM:
        out += '_Foam'
    if variant & VARIANT_REFRACTION:
        out += '_Refraction'
    if variant & VARIANT_GRADIENT:
        out += '_Gradient'
    return out


# How far the sheet has fallen, and therefore how fast it is going and how long it has been going.
#
# Share is this column's fraction of the deepest drop, baked into vertex colour by the mesh. Without
# it every column would accelerate as though it had the longest drop, and a fall over an uneven ledge
# would break up along a line it never reached.
_CODE_FLOW = """
float Speed, Travel;
MobWaterFallFlow(UV.y, Height * Share, LipSpeed, Gravity, Speed, Travel);
FallSpeed = Speed;
return Travel;
"""

# How thin the water has been drawn out, with the amount folded in here rather than at every use.
_CODE_THIN = """
return lerp(1.0f, MobWaterFallThin(Speed, LipSpeed), saturate(Amount));
"""

_CODE_UVS = """
float2 UVA, UVB;
MobWaterFallUVs(UV, Size.x, Streak.x, Streak.y, LipSpeed, Time, Travel, UVA, UVB);
UVSecond = UVB;
return UVA;
"""

# Two already-sampled tangent space normals summed and renormalised. They are stretched along the
# fall for free, the coordinates that read them having been stretched by the water's own maths.
_CODE_NORMAL = """
const float2 XY = (DetailA.xy + DetailB.xy) * max(Strength, 0.0f);
return normalize(float3(XY, 1.0f));
"""

# One noise for the strands and the foam both, and that is not a saving - the strands are where the
# water is and the foam rides on them, so a second noise would put the froth in the gaps.
_CODE_SHEET = """
return MobWaterFallSheet(UV, EdgeFade) * MobWaterFallBreakup(Noise, Thin, Amount);
"""

_CODE_COLUMN = """
return MobWaterFallColumn(Thickness, Thin);
"""

_CODE_COLOR = """
return MobWaterAbsorb(ThinColor, ThickColor, Column, Thickness);
"""

_CODE_GRADIENT_COORD = """
return MobWaterGradientCoord(Column, Thickness);
"""

_CODE_FOAM = """
float Amount, Sharpness;
MobWaterFallUnpackFoam(Packed, Amount, Sharpness);
return MobWaterFallFoam(UV, Noise, Thin, Amount, Sharpness, LipFoam, BaseFoam);
"""

_CODE_OPACITY = """
return MobWaterFallOpacity(MobWaterColumn(SceneDepth, PixelDepth), ClarityDepth, Sheet, Foam,
	MinOpacity);
"""

# The top of the sheet moved onto the water feeding it. World Z, because that is what the query
# answered in and what the offset is measured against.
_CODE_LIP = """
return float3(0.0f, 0.0f, MobWaterFallLip(UV, Offsets.x, Offsets.y, LipFade));
"""

_CODE_GLINT = """
return MobWaterSunGlint(Normal, ViewDirection, SunDirection.xyz, SunColor.rgb, Gloss, Strength,
	0.0f, 0.0f, 1.0f, 1.0f);
"""

_CODE_REFLECTION_UV = """
return MobWaterLongLatUV(ReflectionVector, Params.y);
"""

_CODE_REFLECTION = """
return Sky * (MobWaterFresnel(Normal, ViewDirection) * Params.x * BodyStrength);
"""

# Every term the sheet is built from, one at a time. A fall made of seven things multiplied together
# cannot be debugged by adjusting them: whatever is wrong looks like whatever else you changed.
_CODE_DEBUG = """
if (Mode < 1.5f) return float3(Thin, Thin, Thin);
if (Mode < 2.5f) return float3(Sheet, Sheet, Sheet);
if (Mode < 3.5f) return float3(Foam, Foam, Foam);
if (Mode < 4.5f) return Normal * 0.5f + 0.5f;
if (Mode < 5.5f) return float3(Opacity, Opacity, Opacity);
if (Mode < 6.5f) return float3(Travel, Travel, Travel);
return Glint;
"""


def build_master_material(collection, gradient_asset, gradient_texture, gradient_rows):
    """M_MobWaterFall: a sheet of water, as one translucent material."""
    mat = g.get_or_create_material(g.MAT_ROOT, FALL_NAME)

    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # Two sided, because a fall is seen from behind whenever there is a cave, a ledge or a path
    # behind it - which is most of the reason anyone puts one in a level.
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('translucency_lighting_mode',
                            unreal.TranslucencyLightingMode.TLM_SURFACE)
    # Tangent space, and it is not a choice here the way it is on the surface: the sheet is vertical
    # and curves with the lip, so a world space normal would have to be rebuilt per pixel from a
    # basis the mesh already carries.
    mat.set_editor_property('tangent_space_normal', True)
    mat.set_editor_property('refraction_method', unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)

    # --- where on the sheet -------------------------------------------------
    uv = g.expr(mat, unreal.MaterialExpressionTextureCoordinate, -8, 0)
    uv.set_editor_property('coordinate_index', 0)

    vertex_colour = g.expr(mat, unreal.MaterialExpressionVertexColor, -8, 2)

    time_param = g.collection_param(mat, collection, 'Time', -8, 4)

    size_vec = g.cpd_vector(mat, 'Size', (400.0, 500.0, 0.0, 0.0), CPD_SIZE, 'Fall', -8, 6,
                            'How wide and how tall the sheet is, in world units. The mesh carries a '
                            'unit UV, so how much world that covers is not in the geometry.')

    size = g.expr(mat, unreal.MaterialExpressionComponentMask, -7, 6)
    size.set_editor_property('r', True)
    size.set_editor_property('g', True)
    size.set_editor_property('b', False)
    size.set_editor_property('a', False)
    g.link(size_vec, '', size, '')

    height = g.expr(mat, unreal.MaterialExpressionComponentMask, -7, 8)
    height.set_editor_property('r', False)
    height.set_editor_property('g', True)
    height.set_editor_property('b', False)
    height.set_editor_property('a', False)
    g.link(size_vec, '', height, '')

    lip_speed = g.cpd_scalar(mat, 'LipSpeed', 260.0, CPD_LIP_SPEED, 'Fall', -8, 10,
                             'How fast the water is going as it leaves the lip. Everything the sheet '
                             'does is measured against this.')
    gravity = g.cpd_scalar(mat, 'Gravity', 980.0, CPD_GRAVITY, 'Fall', -8, 12,
                           'What accelerates it on the way down. 0 is water that hangs, which never '
                           'stretches and so never thins or breaks up.')

    # --- how long ago this water left the lip -------------------------------
    #
    # The one number the whole material rests on. A texture panned at a constant rate over a fall
    # reads as a curtain being pulled, because water does not fall at a constant rate.
    flow = g.custom(mat, _CODE_FLOW, g.CMOT.CMOT_FLOAT1,
                    ['UV', 'Height', 'Share', 'LipSpeed', 'Gravity'],
                    [('FallSpeed', g.CMOT.CMOT_FLOAT1)], -6, 4,
                    'How long this water has been falling, and how fast it is now going.',
                    includes=INCLUDES)
    g.link(uv, '', flow, 'UV')
    g.link(height, '', flow, 'Height')
    g.link(vertex_colour, 'R', flow, 'Share')
    g.link(lip_speed, '', flow, 'LipSpeed')
    g.link(gravity, '', flow, 'Gravity')

    thin_amount = g.cpd_scalar(mat, 'ThinAmount', 0.7, CPD_THIN_AMOUNT, 'Fall', -8, 14,
                               'How much the sheet thins as the water is drawn out. 0 holds it at '
                               'full thickness all the way down, which reads as a printed curtain.')

    thin = g.custom(mat, _CODE_THIN, g.CMOT.CMOT_FLOAT1, ['Speed', 'LipSpeed', 'Amount'], [],
                    -5, 6, 'How thin the sheet is here. 1 at the lip.', includes=INCLUDES)
    g.link(flow, 'FallSpeed', thin, 'Speed')
    g.link(lip_speed, '', thin, 'LipSpeed')
    g.link(thin_amount, '', thin, 'Amount')

    # --- the water's own frame ----------------------------------------------
    streak_vec = g.cpd_vector(mat, 'StreakSize', (320.0, 180.0, 0.0, 0.0), CPD_STREAK_SIZE, 'Fall',
                              -8, 16, 'How long a streak is at the lip, and how wide. Below the lip '
                                      'it stretches on its own.')

    streak = g.expr(mat, unreal.MaterialExpressionComponentMask, -7, 16)
    streak.set_editor_property('r', True)
    streak.set_editor_property('g', True)
    streak.set_editor_property('b', False)
    streak.set_editor_property('a', False)
    g.link(streak_vec, '', streak, '')

    uvs = g.custom(mat, _CODE_UVS, g.CMOT.CMOT_FLOAT2,
                   ['UV', 'Size', 'Streak', 'LipSpeed', 'Time', 'Travel'],
                   [('UVSecond', g.CMOT.CMOT_FLOAT2)], -5, 16,
                   'Where the two detail layers are read, in the frame the water itself is in.',
                   includes=INCLUDES)
    g.link(uv, '', uvs, 'UV')
    g.link(size, '', uvs, 'Size')
    g.link(streak, '', uvs, 'Streak')
    g.link(lip_speed, '', uvs, 'LipSpeed')
    g.link(time_param, '', uvs, 'Time')
    g.link(flow, '', uvs, 'Travel')

    normal_texture = g.existing(g.TEX_ROOT + '/T_MobWaterNormal')

    detail_a = g.texture_param(mat, 'DetailNormal', normal_texture, 'Surface', -4, 16,
                               g.ST.SAMPLERTYPE_NORMAL, uvs, '',
                               'The ripples on the sheet, riding down with the water.')
    detail_b = g.texture_param(mat, 'DetailNormalSecond', normal_texture, 'Surface', -4, 18,
                               g.ST.SAMPLERTYPE_NORMAL, uvs, 'UVSecond',
                               'The second layer. One alone is a texture sliding over everything.')

    detail_strength = g.cpd_scalar(mat, 'DetailStrength', 1.0, CPD_DETAIL_STRENGTH, 'Surface',
                                   -4, 20, 'How much of the detail reaches the sheet. 0 is glass.')

    normal = g.custom(mat, _CODE_NORMAL, g.CMOT.CMOT_FLOAT3, ['DetailA', 'DetailB', 'Strength'], [],
                      -3, 17, 'The two layers, as one tangent space normal.', includes=INCLUDES)
    g.link(detail_a, '', normal, 'DetailA')
    g.link(detail_b, '', normal, 'DetailB')
    g.link(detail_strength, '', normal, 'Strength')

    world_normal = g.transform_vector(
        mat, unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT,
        unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, normal, '', -2, 17)

    # --- how much sheet there is --------------------------------------------
    foam_texture = g.existing(g.TEX_ROOT + '/T_MobWaterFoam')

    noise = g.texture_param(mat, 'FoamNoise', foam_texture, 'Foam', -4, 22,
                            g.ST.SAMPLERTYPE_MASKS, uvs, '',
                            'Where the strands are, and therefore where the foam on them is. One '
                            'noise for both: the froth rides the water rather than the gaps.')

    edge_fade = g.cpd_scalar(mat, 'EdgeFade', 0.08, CPD_EDGE_FADE, 'Fall', -4, 24,
                             'How far in from each side the sheet fades, as a fraction of its width. '
                             '0 ends the fall on a polygon edge.')
    breakup = g.cpd_scalar(mat, 'Breakup', 0.35, CPD_BREAKUP, 'Fall', -4, 26,
                           'How far the stretch pulls the sheet apart into strands.')

    sheet = g.custom(mat, _CODE_SHEET, g.CMOT.CMOT_FLOAT1,
                     ['UV', 'EdgeFade', 'Noise', 'Thin', 'Amount'], [], -2, 24,
                     'How much sheet is at this pixel, before anything is drawn on it.',
                     includes=INCLUDES)
    g.link(uv, '', sheet, 'UV')
    g.link(edge_fade, '', sheet, 'EdgeFade')
    g.link(noise, 'R', sheet, 'Noise')
    g.link(thin, '', sheet, 'Thin')
    g.link(breakup, '', sheet, 'Amount')

    # --- what colour the water is -------------------------------------------
    thickness = g.cpd_scalar(mat, 'Thickness', 60.0, CPD_THICKNESS, 'Colour', -6, 30,
                             'How much water is in the sheet at the lip. The colour is graded by '
                             'this the way a body of water is graded by its depth.')

    column = g.custom(mat, _CODE_COLUMN, g.CMOT.CMOT_FLOAT1, ['Thickness', 'Thin'], [], -5, 30,
                      'How much water this pixel is looking through.', includes=INCLUDES)
    g.link(thickness, '', column, 'Thickness')
    g.link(thin, '', column, 'Thin')

    thin_color = g.cpd_vector(mat, 'ThinColor', (0.18, 0.42, 0.42, 1.0), CPD_THIN_COLOR, 'Colour',
                              -6, 32, 'The water where the sheet has been drawn out thinnest.')
    thick_color = g.cpd_vector(mat, 'ThickColor', (0.01, 0.06, 0.11, 1.0), CPD_THICK_COLOR, 'Colour',
                               -6, 34, 'The water at the lip, where there is most of it.')

    base_color = g.custom(mat, _CODE_COLOR, g.CMOT.CMOT_FLOAT3,
                          ['ThinColor', 'ThickColor', 'Column', 'Thickness'], [], -4, 32,
                          'Colour by how much water there is.', includes=INCLUDES)
    g.link(thin_color, '', base_color, 'ThinColor')
    g.link(thick_color, '', base_color, 'ThickColor')
    g.link(column, '', base_color, 'Column')
    g.link(thickness, '', base_color, 'Thickness')

    # --- or the same, as a coordinate into the ramp a body of water reads ----
    #
    # The same atlas and the same rows, so a fall and the river it comes out of can carry one look
    # between them. What is indexed differs, and has to: a fall has no depth to grade by, so it
    # grades by how far the water has been drawn out - which runs the same way, thick to thin.
    gradient_coord = g.custom(mat, _CODE_GRADIENT_COORD, g.CMOT.CMOT_FLOAT1,
                              ['Column', 'Thickness'], [], -4, 34,
                              'How far down the ramp this much water is.', includes=INCLUDES)
    g.link(column, '', gradient_coord, 'Column')
    g.link(thickness, '', gradient_coord, 'Thickness')

    atlas = g.expr(mat, unreal.MaterialExpressionTextureObjectParameter, -4, 36)
    atlas.set_editor_property('parameter_name', 'ColorGradient')
    atlas.set_editor_property('texture', gradient_texture)
    atlas.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    atlas.set_editor_property('group', 'Colour')
    atlas.set_editor_property('desc', 'The palettes this fall can be graded by, one to a row. The '
                                      'same atlas a body of water reads.')

    gradient_row = g.cpd_scalar(mat, 'GradientRow', 0.0, CPD_GRADIENT_ROW, 'Colour', -4, 37,
                                'Which row of the atlas this fall reads. Whole numbers only. %s.'
                                % ', '.join('%d %s' % (i, n) for i, (n, _, _) in enumerate(gradient_rows)))

    ramp_uv = g.expr(mat, unreal.MaterialExpressionGradientCoordinate, -3, 36)
    ramp_uv.set_editor_property('gradient', gradient_asset)
    ramp_uv.set_editor_property('gradient_name', gradient_rows[0][0])
    g.link(gradient_coord, '', ramp_uv, 'Time')
    g.link(atlas, '', ramp_uv, 'Atlas')
    g.link(gradient_row, '', ramp_uv, 'Row')

    ramp = g.expr(mat, unreal.MaterialExpressionTextureSample, -2, 36)
    ramp.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    ramp.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    g.link(ramp_uv, 'UV', ramp, 'UVs')
    g.link(atlas, '', ramp, 'Tex')

    b_gradient = g.static_bool(mat, 'bGradientColor', False, 'Colour', -2, 37, 12,
                               'Grades the water along a ramp rather than between two colours. Off, '
                               'the atlas is not read and the ramp arithmetic leaves the shader.')

    water_color = g.static_switch(mat, b_gradient, ramp, '', base_color, '', -1, 34)

    # --- the foam on it -----------------------------------------------------
    foam_packed = g.cpd_scalar(mat, 'FoamAmount', 60.5, CPD_FOAM_AMOUNT, 'Foam', -4, 40,
                               'Two values in one slot. The whole part is how much foam the streaks '
                               'carry in hundredths; the fraction is how hard a streak edge is over '
                               'its own range. MobWaterFallUnpackFoam splits it.')
    lip_foam = g.cpd_scalar(mat, 'LipFoam', 0.06, CPD_LIP_FOAM, 'Foam', -4, 42,
                            'How far down from the lip the water is white, as a fraction of the drop.')
    base_foam = g.cpd_scalar(mat, 'BaseFoam', 0.18, CPD_BASE_FOAM, 'Foam', -4, 44,
                             'How far up from the plunge it is white. This is what sells the impact.')

    foam = g.custom(mat, _CODE_FOAM, g.CMOT.CMOT_FLOAT1,
                    ['UV', 'Noise', 'Thin', 'Packed', 'LipFoam', 'BaseFoam'], [], -2, 42,
                    'Streaks along the water, white at the lip, white at the plunge.',
                    includes=INCLUDES)
    g.link(uv, '', foam, 'UV')
    g.link(noise, 'R', foam, 'Noise')
    g.link(thin, '', foam, 'Thin')
    g.link(foam_packed, '', foam, 'Packed')
    g.link(lip_foam, '', foam, 'LipFoam')
    g.link(base_foam, '', foam, 'BaseFoam')

    b_foam = g.static_bool(mat, 'bFoam', False, 'Foam', -2, 45, 21,
                           'Streaks, and white where the sheet breaks over the lip and lands. Off, '
                           'the foam maths leaves the shader entirely.')

    out_foam = g.static_switch(mat, b_foam, foam, '', g.const(mat, 0.0, -2, 46), '', -1, 43)

    # White, not off-white. A tint here is one more thing separating solid foam from the flat mark it
    # is meant to be.
    white = g.const3(mat, (1.0, 1.0, 1.0), -1, 40)
    foamed_color = g.lerp(mat, water_color, '', white, '', out_foam, '', 0, 36)

    # How much water is left where the foam is, which is what the sun and the sky are scaled by.
    # Foam is froth: it scatters rather than reflecting, and water shading through it is what makes
    # foam read as a texture on the sheet rather than as something on it.
    water_share = g.sub(mat, g.const(mat, 1.0, 0, 40), '', out_foam, '', 0, 41)

    # --- how opaque ---------------------------------------------------------
    scene_depth = g.expr(mat, unreal.MaterialExpressionSceneDepth, -4, 48)
    pixel_depth = g.expr(mat, unreal.MaterialExpressionPixelDepth, -4, 49)

    clarity = g.cpd_scalar(mat, 'ClarityDepth', 120.0, CPD_CLARITY_DEPTH, 'Colour', -4, 50,
                           'The water column over which the sheet stops reading as touching what is '
                           'behind it. This is what softens the line where it meets rock and pool.')
    min_opacity = g.cpd_scalar(mat, 'MinOpacity', 0.25, CPD_MIN_OPACITY, 'Colour', -4, 51,
                               'How opaque the sheet is where it is running over something.')

    opacity = g.custom(mat, _CODE_OPACITY, g.CMOT.CMOT_FLOAT1,
                       ['SceneDepth', 'PixelDepth', 'ClarityDepth', 'Sheet', 'Foam', 'MinOpacity'],
                       [], 0, 48, 'How much of what is behind this pixel the sheet has taken away.',
                       includes=INCLUDES)
    g.link(scene_depth, '', opacity, 'SceneDepth')
    g.link(pixel_depth, '', opacity, 'PixelDepth')
    g.link(clarity, '', opacity, 'ClarityDepth')
    g.link(sheet, '', opacity, 'Sheet')
    g.link(out_foam, '', opacity, 'Foam')
    g.link(min_opacity, '', opacity, 'MinOpacity')

    # --- refraction ---------------------------------------------------------
    #
    # Worth less on a fall than on a pool - a sheet is mostly moving foam and there is rarely much
    # behind it to bend - which is exactly why it stays a compiled variant rather than a default.
    refraction_strength = g.cpd_scalar(mat, 'RefractionStrength', 0.2, CPD_REFRACTION_STRENGTH,
                                       'Refraction', -4, 54,
                                       'How far the sheet bends what is behind it.')

    # In Pixel Normal Offset mode the refraction input scales the offset: 1.0 is none and 2.0 is a
    # scale of one. It is not an index of refraction despite the pin's name.
    water_offset = g.lerp(mat, g.const(mat, 1.0, -3, 54), '', g.const(mat, 2.0, -3, 55), '',
                          refraction_strength, '', -2, 54)

    b_refraction = g.static_bool(mat, 'bRefraction', False, 'Refraction', -2, 56, 22,
                                 'Bends what is behind the sheet. Needs scene colour in the '
                                 'translucent pass; off, nothing reads it.')

    out_refraction = g.static_switch(mat, b_refraction, water_offset, '',
                                     g.const(mat, 1.0, -2, 57), '', 0, 55)

    # --- the sun on it ------------------------------------------------------
    sun_direction = g.collection_param(mat, collection, 'SunDirection', -4, 60)
    sun_color = g.collection_param(mat, collection, 'SunColor', -4, 61)

    view_dir = g.expr(mat, unreal.MaterialExpressionCameraVectorWS, -4, 62)

    glint_gloss = g.cpd_scalar(mat, 'GlintGloss', 180.0, CPD_GLINT_GLOSS, 'Surface', -4, 63,
                               'How tight the sun lobe is. Broader than a pool wants: a fall is '
                               'never flat, so a tight lobe lands on nothing.')
    glint_strength = g.cpd_scalar(mat, 'GlintStrength', 1.6, CPD_GLINT_STRENGTH, 'Surface', -4, 64,
                                  'How bright the sun is on the sheet.')

    glint = g.custom(mat, _CODE_GLINT, g.CMOT.CMOT_FLOAT3,
                     ['Normal', 'ViewDirection', 'SunDirection', 'SunColor', 'Gloss', 'Strength'],
                     [], -2, 61, 'The sun on the water, added rather than reflected.',
                     includes=INCLUDES)
    g.link(world_normal, '', glint, 'Normal')
    g.link(view_dir, '', glint, 'ViewDirection')
    g.link(sun_direction, '', glint, 'SunDirection')
    g.link(sun_color, '', glint, 'SunColor')
    g.link(glint_gloss, '', glint, 'Gloss')
    g.link(glint_strength, '', glint, 'Strength')

    # --- the sky in it ------------------------------------------------------
    #
    # A vertical sheet reflects the horizon rather than the zenith, so this reads a very different
    # part of the same sky the surface does - and that is the whole reason it is worth having on a
    # fall at all.
    reflection_params = g.collection_param(mat, collection, 'ReflectionParams', -4, 66)
    reflection_vector = g.expr(mat, unreal.MaterialExpressionReflectionVectorWS, -4, 67)

    reflection_uv = g.custom(mat, _CODE_REFLECTION_UV, g.CMOT.CMOT_FLOAT2,
                             ['ReflectionVector', 'Params'], [], -3, 67,
                             'Where the reflected ray lands on the sky.', includes=INCLUDES)
    g.link(reflection_vector, '', reflection_uv, 'ReflectionVector')
    g.link(reflection_params, '', reflection_uv, 'Params')

    sky = g.texture_param(mat, 'ReflectionTexture', g.existing(g.TEX_ROOT + '/T_MobWaterSky'),
                          'Reflection', -2, 67, g.ST.SAMPLERTYPE_COLOR, reflection_uv, '',
                          'The sky this fall reflects, as a long-latitude image.')

    reflection_strength = g.cpd_scalar(mat, 'ReflectionStrength', 0.6, CPD_REFLECTION_STRENGTH,
                                       'Reflection', -2, 69, 'How much sky this fall reflects.')

    reflection = g.custom(mat, _CODE_REFLECTION, g.CMOT.CMOT_FLOAT3,
                          ['Sky', 'Normal', 'ViewDirection', 'Params', 'BodyStrength'], [], -1, 67,
                          'The sky, weighted by how obliquely the sheet is being seen.',
                          includes=INCLUDES)
    g.link(sky, '', reflection, 'Sky')
    g.link(world_normal, '', reflection, 'Normal')
    g.link(view_dir, '', reflection, 'ViewDirection')
    g.link(reflection_params, '', reflection, 'Params')
    g.link(reflection_strength, '', reflection, 'BodyStrength')

    b_reflection = g.static_bool(mat, 'bReflection', True, 'Reflection', -1, 69, 25,
                                 'Reflects a sky texture. Off, the sample and its maths leave the '
                                 'shader and the water has only its own colour and the sun.')

    lit = g.add(mat, glint, '', reflection, '', 0, 64)
    surface_light = g.static_switch(mat, b_reflection, lit, '', glint, '', 1, 64)

    # Neither the sun nor the sky reaches the water under solid foam.
    surface_light = g.mul(mat, surface_light, '', water_share, '', 2, 64)

    # --- the join to the water above ----------------------------------------
    lip_offsets_vec = g.cpd_vector(mat, 'LipOffset', (0.0, 0.0, 0.0, 0.0), CPD_LIP_OFFSET, 'Join',
                                   -6, 72, 'Where the water feeding this fall actually is, at each '
                                           'end of the lip. Written every frame from the same query '
                                           'buoyancy reads.')

    lip_offsets = g.expr(mat, unreal.MaterialExpressionComponentMask, -5, 72)
    lip_offsets.set_editor_property('r', True)
    lip_offsets.set_editor_property('g', True)
    lip_offsets.set_editor_property('b', False)
    lip_offsets.set_editor_property('a', False)
    g.link(lip_offsets_vec, '', lip_offsets, '')

    lip_fade = g.cpd_scalar(mat, 'LipFade', 0.25, CPD_LIP_FADE, 'Join', -5, 74,
                            'How far down the sheet that offset dies away. The top of a fall belongs '
                            'to the river and the bottom belongs to the ground.')

    wpo = g.custom(mat, _CODE_LIP, g.CMOT.CMOT_FLOAT3, ['UV', 'Offsets', 'LipFade'], [], -3, 72,
                   'The top of the sheet, moved onto the water feeding it.', includes=INCLUDES)
    g.link(uv, '', wpo, 'UV')
    g.link(lip_offsets, '', wpo, 'Offsets')
    g.link(lip_fade, '', wpo, 'LipFade')

    # --- outputs ------------------------------------------------------------
    unlit = g.cpd_scalar(mat, 'Unlit', 0.0, CPD_UNLIT, 'Colour', 0, 32,
                         'How much of the colour is emitted rather than lit. 1 is a flat colour with '
                         'no ambient tint and no dark side.')

    lit_share = g.sub(mat, g.const(mat, 1.0, 0, 31), '', unlit, '', 1, 31)

    lit_color = g.mul(mat, foamed_color, '', lit_share, '', 1, 33)
    emitted_color = g.mul(mat, foamed_color, '', unlit, '', 1, 35)

    emissive = g.add(mat, surface_light, '', emitted_color, '', 3, 64)

    roughness = g.cpd_scalar(mat, 'Roughness', 0.12, CPD_ROUGHNESS, 'Surface', 0, 26,
                             'Falling water is broken rather than glassy, so it is rougher than a '
                             'pool by default.')
    out_roughness = g.lerp(mat, roughness, '', g.const(mat, 1.0, 0, 27), '', out_foam, '', 1, 26)

    # Flattened where the foam is solid. A lit bump under a white band is the pattern that makes foam
    # read as a texture rather than as something floating on the water.
    out_normal = g.lerp(mat, normal, '', g.const3(mat, (0.0, 0.0, 1.0), 0, 20), '', out_foam, '',
                        1, 19)

    # --- debug views --------------------------------------------------------
    debug_mode = g.scalar_param(mat, 'DebugMode', 1.0, 'Debug', 2, 76,
                                '1 thinning, 2 sheet, 3 foam, 4 normal, 5 opacity, 6 how long the '
                                'water has been falling, 7 glint.')

    debug = g.custom(mat, _CODE_DEBUG, g.CMOT.CMOT_FLOAT3,
                     ['Mode', 'Thin', 'Sheet', 'Foam', 'Normal', 'Opacity', 'Travel', 'Glint'],
                     [], 3, 76, 'One term of the sheet at a time.', includes=INCLUDES)
    g.link(debug_mode, '', debug, 'Mode')
    g.link(thin, '', debug, 'Thin')
    g.link(sheet, '', debug, 'Sheet')
    g.link(out_foam, '', debug, 'Foam')
    g.link(world_normal, '', debug, 'Normal')
    g.link(opacity, '', debug, 'Opacity')
    g.link(flow, '', debug, 'Travel')
    g.link(glint, '', debug, 'Glint')

    b_debug = g.static_bool(mat, 'bDebugView', False, 'Debug', 3, 78, 90,
                            'Replaces the sheet with one of the values it is built from. Off, none '
                            'of this is compiled.')

    final_color = g.static_switch(mat, b_debug, g.const3(mat, (0.0, 0.0, 0.0), 3, 79), '',
                                  lit_color, '', 4, 33)
    final_opacity = g.static_switch(mat, b_debug, g.const(mat, 1.0, 3, 80), '', opacity, '', 4, 48)
    final_emissive = g.static_switch(mat, b_debug, debug, '', emissive, '', 4, 64)

    # --- hooks --------------------------------------------------------------
    sig = {'Color': (final_color, ''), 'Opacity': (final_opacity, ''),
           'Roughness': (out_roughness, ''), 'Normal': (out_normal, ''),
           'Refraction': (out_refraction, ''), 'Emissive': (final_emissive, ''),
           'Thin': (thin, ''), 'Sheet': (sheet, ''), 'Foam': (out_foam, ''),
           'Travel': (flow, ''), 'Glint': (glint, '')}
    sig = water_hooks.apply(mat, water_hooks.OUTPUT, sig, 5, 120)

    # Added rather than replacing, so a project's own displacement stacks with the sheet's own.
    hooked = water_hooks.apply(mat, water_hooks.WPO, sig, 5, 140).get('WorldPositionOffset')
    if hooked is not None:
        wpo = g.add(mat, wpo, '', hooked[0], hooked[1], 5, 142)

    g.link_property(mat, wpo, '', g.MP.MP_WORLD_POSITION_OFFSET)
    g.link_property(mat, sig['Color'][0], sig['Color'][1], g.MP.MP_BASE_COLOR)
    g.link_property(mat, sig['Opacity'][0], sig['Opacity'][1], g.MP.MP_OPACITY)
    g.link_property(mat, sig['Roughness'][0], sig['Roughness'][1], g.MP.MP_ROUGHNESS)
    g.link_property(mat, sig['Normal'][0], sig['Normal'][1], g.MP.MP_NORMAL)
    g.link_property(mat, sig['Refraction'][0], sig['Refraction'][1], g.MP.MP_REFRACTION)
    g.link_property(mat, sig['Emissive'][0], sig['Emissive'][1], g.MP.MP_EMISSIVE_COLOR)

    g.spread(g.MEL.get_material_expressions(mat))
    g.MEL.recompile_material(mat)
    g.save(mat)

    return mat


def build_material_instances(master):
    """One instance per feature set, minus the plain one, which is the master itself."""
    built = []

    for variant in range(VARIANT_NUM):
        suffix = variant_suffix(variant)
        if not suffix:
            # Variant zero is the master. An instance of it with every switch at its default would be
            # a second asset holding no information, and the settings already point at the master.
            continue

        name = 'MI_MobWaterFall%s' % suffix

        instance = g.get_or_create_instance(g.MAT_ROOT, name, master)

        g.MEL.set_material_instance_static_switch_parameter_value(
            instance, 'bFoam', bool(variant & VARIANT_FOAM))
        g.MEL.set_material_instance_static_switch_parameter_value(
            instance, 'bRefraction', bool(variant & VARIANT_REFRACTION))
        g.MEL.set_material_instance_static_switch_parameter_value(
            instance, 'bGradientColor', bool(variant & VARIANT_GRADIENT))

        g.MEL.update_material_instance(instance)
        g.save(instance)
        built.append(name)

    # One debug instance. Assign it to a fall's material slot to see what the sheet is made of, then
    # put the real one back.
    instance = g.get_or_create_instance(g.MAT_ROOT, 'MI_MobWaterFall_Debug', master)
    g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bFoam', True)
    g.MEL.set_material_instance_static_switch_parameter_value(instance, 'bDebugView', True)
    g.MEL.update_material_instance(instance)
    g.save(instance)
    built.append('MI_MobWaterFall_Debug')

    return built


def build_all(collection, gradient_asset, gradient_texture, gradient_rows):
    master = build_master_material(collection, gradient_asset, gradient_texture, gradient_rows)
    g.log('  fall master %s' % master.get_path_name())

    for instance in build_material_instances(master):
        g.log('  fall %s' % instance)

    return master
