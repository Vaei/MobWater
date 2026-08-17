# Copyright (c) Jared Taylor. All Rights Reserved

"""What MobWater costs, measured rather than claimed.

Two numbers matter on this renderer and neither is obvious from looking at a material. How many
shaders exist, because every feature that is a compiled variant multiplies them and they are what a
cook spends its time on. And what the textures weigh, because a plugin that quietly adds thirty
megabytes to every level is a plugin nobody keeps.

Run from Water > Report Cost, or:

    import mob_water_report
    mob_water_report.run()

Instruction counts come out of the editor's own estimate, which over-reports and is not the same
number the platform's compiler produces. They are a baseline to hold steady, not ground truth - and
they are measured against whatever preview platform is active, so a figure quoted anywhere should say
which one it came from.
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

MAT_ROOT = '/MobWater/Materials'
TEX_ROOT = '/MobWater/Textures'

MASTERS = [
    'M_MobWater',
    'M_MobWaterUnderwater',
    'M_MobWaterRippleStep',
    'M_MobWaterRippleCopy',
    'M_MobWaterRippleStamp',
]


def _log(message):
    unreal.log('[MobWater] {0}'.format(message))


def _assets_in(path):
    """Every asset directly under a path.

    The registry is scanned first because list_assets reads it, and in a commandlet it has not
    scanned - so it answers with nothing at all for a folder that is plainly full. That failure is
    silent and reads as "the generator produced no instances" rather than as "nobody looked".
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([path], force_rescan=True)

    return [p for p in EAL.list_assets(path, recursive=False, include_folder=False)]


def masters():
    """Every master, with what the editor estimates it costs."""
    _log('Masters')

    total_shaders = 0

    for name in MASTERS:
        material = unreal.load_asset(MAT_ROOT + '/' + name)
        if material is None:
            _log('  --  {0} has not been generated'.format(name))
            continue

        stats = MEL.get_statistics(material)

        _log('  {0}: {1} vertex, {2} pixel, {3} samplers, {4} interpolator scalars'.format(
            name,
            stats.get_editor_property('num_vertex_shader_instructions'),
            stats.get_editor_property('num_pixel_shader_instructions'),
            stats.get_editor_property('num_samplers'),
            stats.get_editor_property('num_interpolator_scalars')))

        total_shaders += 1

    return total_shaders


def permutations():
    """How many material instances exist, which is how many shader maps a cook builds."""
    _log('Permutations')

    instances = [p for p in _assets_in(MAT_ROOT) if '/MI_' in p]

    _log('  {0} instances'.format(len(instances)))

    # Grouped by what they are of rather than listed, because sixteen paths in a log is a wall and
    # the number per shape is the thing anyone is actually checking.
    shapes = {}
    for path in instances:
        name = path.rsplit('/', 1)[-1].split('.')[0]
        parts = name.split('_')
        shape = parts[2] if len(parts) > 2 else 'Unknown'
        shapes[shape] = shapes.get(shape, 0) + 1

    for shape in sorted(shapes):
        _log('    {0}: {1}'.format(shape, shapes[shape]))

    return len(instances)


def texture_memory():
    """What the textures weigh, in the editor's own accounting."""
    _log('Textures')

    total = 0

    for path in _assets_in(TEX_ROOT):
        texture = unreal.load_asset(path)
        if texture is None:
            continue

        name = path.rsplit('/', 1)[-1].split('.')[0]

        if isinstance(texture, unreal.TextureRenderTarget2D):
            size_x = texture.get_editor_property('size_x')
            size_y = texture.get_editor_property('size_y')

            # Render targets are not in the texture accounting, being allocated rather than loaded,
            # so they are counted by hand. Four half floats a texel is what RTF_RGBA16F costs.
            bytes_used = size_x * size_y * 8
            total += bytes_used

            _log('  {0}: {1}x{2} render target, {3:.2f} MB'.format(
                name, size_x, size_y, bytes_used / (1024.0 * 1024.0)))
            continue

        if isinstance(texture, unreal.Texture2D):
            bytes_used = texture.blueprint_get_memory_size()
            total += bytes_used

            # Resident size, which is not necessarily the authored size. Outside a live editor a
            # texture's platform data may hold only its smallest mip, so this can report 32x32 for
            # something authored at 256 - it is understated, never overstated. The authored size is
            # not reachable from Python: Source is protected and ImportedSize is not exposed, so the
            # honest thing is to say which number this is rather than to invent the other one.
            _log('  {0}: {1}x{2} resident, {3:.2f} MB'.format(
                name,
                texture.blueprint_get_size_x(),
                texture.blueprint_get_size_y(),
                bytes_used / (1024.0 * 1024.0)))

    _log('  total {0:.2f} MB (resident; run this from the editor for the real figure)'
         .format(total / (1024.0 * 1024.0)))

    return total


def run():
    _log('Cost report')
    _log('  (editor estimates, on whatever preview platform is active - not the target compiler)')

    masters()
    permutations()
    texture_memory()

    _log('Done.')
    return True


if __name__ == '__main__':
    run()
