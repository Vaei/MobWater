# Copyright (c) Jared Taylor. All Rights Reserved

"""The graph primitives every MobWater generator is built out of.

A module of its own rather than helpers exported from whichever generator was written first. Three
scripts author materials here - the surface, the ripple field and the underwater volume - and two
copies of a node factory that drift is a worse outcome than the import.

Nothing in here knows what water is. It knows how to make a node, connect two of them, and rebuild
an asset in place without breaking the instances that point at it.
"""

import unreal

import mob_water_version

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

FIT = unreal.FunctionInputType
CMOT = unreal.CustomMaterialOutputType
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType

ROOT = '/MobWater'
MAT_ROOT = ROOT + '/Materials'
FN_ROOT = ROOT + '/Functions'
TEX_ROOT = ROOT + '/Textures'
MESH_ROOT = ROOT + '/Meshes'

# Material nodes are far wider than they are tall, so columns authored on a loose grid overlap once
# the nodes in them are real. _spread relays them onto this pitch.
COLUMN_PITCH = 460

DEFAULT_INCLUDES = ['/MobWater/Public/MobWaterWaves.ush']


# ---------------------------------------------------------------------------
# Asset plumbing
# ---------------------------------------------------------------------------

def tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log('[MobWater] ' + str(msg))


def log_error(msg):
    unreal.log_error('[MobWater] ' + str(msg))


def clear_function(fn):
    """Empties a MaterialFunction.

    delete_all_material_expressions_in_function leaves nodes behind, so re-running a builder silently
    accumulates duplicate function inputs, and the caller's connections then land on whichever copy
    the name lookup hits first. Loop until the graph is actually empty.
    """
    for _ in range(16):
        exprs = MEL.get_material_function_expressions(fn)
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression_in_function(fn, e)
    raise RuntimeError('could not empty material function %s' % fn.get_name())


def clear_material(mat):
    """Empties a Material. Same caveat as clear_function."""
    for _ in range(16):
        exprs = MEL.get_material_expressions(mat)
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression(mat, e)
    raise RuntimeError('could not empty material %s' % mat.get_name())


def existing(path):
    """The asset at a path, or None.

    Loaded rather than asked about. does_asset_exist reads the asset registry, and a registry that
    has not scanned - which is every commandlet run - answers no for assets that are plainly there.
    The create call then fails, because it cannot prompt about overwriting with nobody watching.
    """
    return unreal.load_asset(path)


def get_or_create_function(name, description=''):
    path = FN_ROOT + '/' + name
    fn = existing(path)
    if fn is not None:
        clear_function(fn)
    else:
        fn = tools().create_asset(name, FN_ROOT, unreal.MaterialFunction,
                                  unreal.MaterialFunctionFactoryNew())
    fn.set_editor_property('description', description)
    fn.set_editor_property('expose_to_library', True)
    fn.set_editor_property('library_categories_text', ['MobWater'])
    return fn


def get_or_create_material(package_path, name):
    path = package_path + '/' + name
    mat = existing(path)
    if mat is not None:
        clear_material(mat)
    else:
        mat = tools().create_asset(name, package_path, unreal.Material,
                                   unreal.MaterialFactoryNew())
    return mat


def get_or_create_instance(package_path, name, parent):
    path = package_path + '/' + name
    mi = existing(path)
    if mi is None:
        mi = tools().create_asset(name, package_path, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    return mi


def save(asset):
    mob_water_version.stamp(asset)
    EAL.save_loaded_asset(asset, only_if_is_dirty=False)


# ---------------------------------------------------------------------------
# Expression helpers
# ---------------------------------------------------------------------------

def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, int(x), y)


def fnexpr(fn, cls, x, y):
    return MEL.create_material_expression_in_function(fn, cls, int(x), y)


def spread(exprs):
    """Relays the graph out so authored columns become evenly spaced ones.

    Laid out to the left of the origin, because the material's own output node sits at zero and
    cannot be moved. Numbering the columns from zero upwards puts the entire graph to the right of
    the thing it feeds, so every wire runs backwards across the window.
    """
    columns = sorted({e.get_editor_property('material_expression_editor_x') for e in exprs})
    count = len(columns)
    placement = {x: (i - count) * COLUMN_PITCH for i, x in enumerate(columns)}
    for e in exprs:
        e.set_editor_property('material_expression_editor_x',
                              placement[e.get_editor_property('material_expression_editor_x')])


def finish_fn(fn):
    spread(MEL.get_material_function_expressions(fn))
    MEL.update_material_function(fn)


def link(src, src_out, dst, dst_in):
    if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
        raise RuntimeError('failed to connect %s.%s -> %s.%s'
                           % (src.get_name(), src_out, dst.get_name(), dst_in))


def link_property(mat, src, src_out, prop):
    if not MEL.connect_material_property(src, src_out, prop):
        raise RuntimeError('failed to connect %s.%s -> %s' % (src.get_name(), src_out, prop))


def vec4(x, y, z, w):
    """PreviewValue is an FVector4f, whose Python binding takes no constructor arguments."""
    v = unreal.Vector4f()
    v.set_editor_property('x', float(x))
    v.set_editor_property('y', float(y))
    v.set_editor_property('z', float(z))
    v.set_editor_property('w', float(w))
    return v


def fn_input(fn, name, input_type, x, y, sort, default=None, description=''):
    e = fnexpr(fn, unreal.MaterialExpressionFunctionInput, x, y)
    e.set_editor_property('input_name', name)
    e.set_editor_property('input_type', input_type)
    e.set_editor_property('sort_priority', sort)
    e.set_editor_property('description', description)
    if default is not None:
        if isinstance(default, (int, float)):
            default = (float(default), 0.0, 0.0, 0.0)
        while len(default) < 4:
            default = tuple(default) + (0.0,)
        e.set_editor_property('preview_value', vec4(*default[:4]))
        e.set_editor_property('use_preview_value_as_default', True)
    return e


def fn_output(fn, name, x, y, sort):
    e = fnexpr(fn, unreal.MaterialExpressionFunctionOutput, x, y)
    e.set_editor_property('output_name', name)
    e.set_editor_property('sort_priority', sort)
    return e


def custom(owner, code, output_type, input_names, extra_outputs, x, y, description, includes=None):
    """A Custom node inside a MaterialFunction or a Material.

    A Custom node emits its whole body whether or not an output is read, so anything that should be
    free when a feature is off needs its own node behind a static switch rather than a spare output
    on a shared one.
    """
    if isinstance(owner, unreal.MaterialFunction):
        e = fnexpr(owner, unreal.MaterialExpressionCustom, x, y)
    else:
        e = expr(owner, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property('code', code)
    e.set_editor_property('output_type', output_type)
    e.set_editor_property('description', description)
    # FCustomInput/FCustomOutput are plain USTRUCTs, so they take no constructor kwargs.
    ins = []
    for n in input_names:
        s = unreal.CustomInput()
        s.set_editor_property('input_name', n)
        ins.append(s)
    outs = []
    for n, t in extra_outputs:
        s = unreal.CustomOutput()
        s.set_editor_property('output_name', n)
        s.set_editor_property('output_type', t)
        outs.append(s)
    e.set_editor_property('inputs', ins)
    e.set_editor_property('additional_outputs', outs)
    e.set_editor_property('include_file_paths', includes if includes is not None else DEFAULT_INCLUDES)
    return e


def const(owner, value, x, y):
    e = _owner_expr(owner, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property('r', float(value))
    return e


def const2(owner, xy, x, y):
    e = _owner_expr(owner, unreal.MaterialExpressionConstant2Vector, x, y)
    e.set_editor_property('r', float(xy[0]))
    e.set_editor_property('g', float(xy[1]))
    return e


def const3(owner, rgb, x, y):
    e = _owner_expr(owner, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property('constant', unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


def _owner_expr(owner, cls, x, y):
    if isinstance(owner, unreal.MaterialFunction):
        return fnexpr(owner, cls, x, y)
    return expr(owner, cls, x, y)


def static_switch(owner, value_expr, true_src, true_out, false_src, false_out, x, y):
    sw = _owner_expr(owner, unreal.MaterialExpressionStaticSwitch, x, y)
    link(value_expr, '', sw, 'Value')
    link(true_src, true_out, sw, 'True')
    link(false_src, false_out, sw, 'False')
    return sw


def binary(owner, cls, a, a_out, b, b_out, x, y):
    e = _owner_expr(owner, cls, x, y)
    link(a, a_out, e, 'A')
    link(b, b_out, e, 'B')
    return e


def mul(owner, a, a_out, b, b_out, x, y):
    return binary(owner, unreal.MaterialExpressionMultiply, a, a_out, b, b_out, x, y)


def add(owner, a, a_out, b, b_out, x, y):
    return binary(owner, unreal.MaterialExpressionAdd, a, a_out, b, b_out, x, y)


def sub(owner, a, a_out, b, b_out, x, y):
    return binary(owner, unreal.MaterialExpressionSubtract, a, a_out, b, b_out, x, y)


def divide(owner, a, a_out, b, b_out, x, y):
    return binary(owner, unreal.MaterialExpressionDivide, a, a_out, b, b_out, x, y)


def saturate_expr(owner, src, src_out, x, y):
    e = _owner_expr(owner, unreal.MaterialExpressionSaturate, x, y)
    link(src, src_out, e, '')
    return e


def static_bool(mat, name, default, group, x, y, sort=0, description=''):
    """A feature switch.

    Static rather than dynamic because a branch the compiler can fold is a branch that leaves the
    shader entirely, taking its texture samples with it. A dynamic one is an amount of zero that is
    still paid for on every pixel.
    """
    e = expr(mat, unreal.MaterialExpressionStaticBoolParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', bool(default))
    e.set_editor_property('group', group)
    e.set_editor_property('sort_priority', sort)
    e.set_editor_property('desc', description)
    return e


def scalar_param(mat, name, default, group, x, y, description=''):
    """A plain scalar parameter, tuned on the instance rather than per body of water.

    Custom primitive data is for what differs between one pool and the next. Anything that has to
    match across a level - the size of the detail tiling, how strong it is - belongs on the instance,
    or two bodies side by side disagree about what water looks like.
    """
    e = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', float(default))
    e.set_editor_property('group', group)
    e.set_editor_property('desc', description)
    return e


def cpd_scalar(mat, name, default, index, group, x, y, description=''):
    """A scalar parameter fed from custom primitive data.

    Per instance data rather than a dynamic material instance per body of water, because the whole
    arrangement here is that a hundred pools are one material and one draw call's worth of state.
    """
    e = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', float(default))
    e.set_editor_property('group', group)
    e.set_editor_property('desc', description)
    e.set_editor_property('use_custom_primitive_data', True)
    e.set_editor_property('primitive_data_index', int(index))
    return e


def cpd_vector(mat, name, default, index, group, x, y, description=''):
    e = expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('default_value', unreal.LinearColor(*default))
    e.set_editor_property('group', group)
    e.set_editor_property('desc', description)
    e.set_editor_property('use_custom_primitive_data', True)
    e.set_editor_property('primitive_data_index', int(index))
    return e


def collection_param(mat, collection, name, x, y):
    """One value out of the collection every water material shares."""
    e = _owner_expr(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    e.set_editor_property('collection', collection)
    e.set_editor_property('parameter_name', name)
    return e


def texture_param(mat, name, texture, group, x, y, sampler_type, uv=None, uv_out='', description=''):
    """A texture parameter sampled through the shared wrap sampler.

    Shared:Wrap rather than the texture's own, because the sixteen sampler budget is what binds on
    this renderer and a Custom node cannot use a shared sampler at all - which is why every tap
    happens in a node like this one and the maths takes already-sampled values.
    """
    e = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property('parameter_name', name)
    e.set_editor_property('texture', texture)
    e.set_editor_property('sampler_type', sampler_type)
    e.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    e.set_editor_property('group', group)
    e.set_editor_property('desc', description)

    if uv is not None:
        link(uv, uv_out, e, 'UVs')

    return e


def vector_param4(mat, name, default, group, x, y, description=''):
    """A vector parameter, as a genuine float4.

    A vector parameter's unnamed output is RGB. Handing it to anything expecting a float4 compiles to
    "vector swizzle 'w' is out of bounds" from inside the generated material, pointing at a line of
    HLSL nobody wrote - so the alpha is appended back on here rather than discovered there. Collection
    parameters do not have this problem, which is what makes it so easy to hit.
    """
    param = expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    param.set_editor_property('parameter_name', name)
    param.set_editor_property('default_value', unreal.LinearColor(*default))
    param.set_editor_property('group', group)
    param.set_editor_property('desc', description)

    appended = expr(mat, unreal.MaterialExpressionAppendVector, x + 1, y)
    link(param, '', appended, 'A')
    link(param, 'A', appended, 'B')

    return appended


def lerp(owner, a, a_out, b, b_out, alpha, alpha_out, x, y):
    e = _owner_expr(owner, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, a_out, e, 'A')
    link(b, b_out, e, 'B')
    link(alpha, alpha_out, e, 'Alpha')
    return e


def vertex_interpolator(mat, src, src_out, x, y):
    """Moves an evaluation into the vertex shader and interpolates the result.

    The wave set is eight sines and eight cosines. Feeding the pixel shader from the same node that
    feeds the world position offset evaluates all of it twice - once per vertex, where it belongs,
    and once per pixel, where it is hundreds of instructions to recompute something the vertices
    already knew.
    """
    e = expr(mat, unreal.MaterialExpressionVertexInterpolator, x, y)
    link(src, src_out, e, '')
    return e


def transform_vector(mat, source, dest, src_expr, src_out, x, y):
    e = expr(mat, unreal.MaterialExpressionTransform, x, y)
    e.set_editor_property('transform_source_type', source)
    e.set_editor_property('transform_type', dest)
    link(src_expr, src_out, e, '')
    return e


def transform_position(mat, source, dest, src_expr, x, y):
    e = expr(mat, unreal.MaterialExpressionTransformPosition, x, y)
    e.set_editor_property('transform_source_type', source)
    e.set_editor_property('transform_type', dest)
    link(src_expr, '', e, 'Input')
    return e


# ---------------------------------------------------------------------------
# Parameter collection
# ---------------------------------------------------------------------------

def ensure_collection(package_path, name, vectors, scalars):
    """Adds any missing parameter, removes any the materials no longer read, and touches nothing else.

    Rebuilding this the way the materials are rebuilt would issue new GUIDs, and every material
    referencing the old ones would silently compile them as zero. Additive, plus a prune.

    A parameter left behind in here looks exactly like a setting, which is worse than it sounds: it
    is a knob wired to nothing that reads as a knob wired to something.
    """
    path = package_path + '/' + name
    mpc = existing(path)
    if mpc is None:
        mpc = tools().create_asset(name, package_path, unreal.MaterialParameterCollection,
                                   unreal.MaterialParameterCollectionFactoryNew())

    existing_vectors = list(mpc.get_editor_property('vector_parameters'))
    present = {str(p.get_editor_property('parameter_name')) for p in existing_vectors}
    added = []

    for vec_name, default in vectors:
        if vec_name in present:
            continue
        param = unreal.CollectionVectorParameter()
        param.set_editor_property('parameter_name', vec_name)
        param.set_editor_property('default_value', unreal.LinearColor(*default))
        existing_vectors.append(param)
        added.append(vec_name)

    existing_scalars = list(mpc.get_editor_property('scalar_parameters'))
    present = {str(p.get_editor_property('parameter_name')) for p in existing_scalars}

    for scalar_name, default in scalars:
        if scalar_name in present:
            continue
        param = unreal.CollectionScalarParameter()
        param.set_editor_property('parameter_name', scalar_name)
        param.set_editor_property('default_value', float(default))
        existing_scalars.append(param)
        added.append(scalar_name)

    wanted_vectors = {n for n, _ in vectors}
    wanted_scalars = {n for n, _ in scalars}

    kept_vectors = [p for p in existing_vectors
                    if str(p.get_editor_property('parameter_name')) in wanted_vectors]
    kept_scalars = [p for p in existing_scalars
                    if str(p.get_editor_property('parameter_name')) in wanted_scalars]

    removed = (len(existing_vectors) - len(kept_vectors)) + (len(existing_scalars) - len(kept_scalars))

    mpc.set_editor_property('vector_parameters', kept_vectors)
    mpc.set_editor_property('scalar_parameters', kept_scalars)

    save(mpc)

    if added:
        log('%s: added %s' % (name, ', '.join(added)))
    if removed:
        log('%s: removed %d parameter(s) nothing reads' % (name, removed))

    return mpc
