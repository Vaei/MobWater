# Copyright (c) Jared Taylor. All Rights Reserved

"""Authors the two textures the surface needs, as PNGs, and imports them.

Written out and imported rather than built in memory because the editor's Python has no way to hand
raw pixels to a texture: the source data setters are C++ only. A PNG through the normal import path
is the one route that works, and it leaves something a person can look at when the water does not
look right.

Both tile. Every octave's lattice period divides the texture, or there is a seam down the middle of
every body of water in the level - which on a surface this large is the first thing anyone sees.

No third party dependency. The editor's Python has no numpy on every machine, and a texture that
only generates on some of them is worse than one that generates slowly on all of them.
"""

import math
import os
import struct
import zlib

import unreal

import mob_water_version

EAL = unreal.EditorAssetLibrary

TEX_ROOT = '/MobWater/Textures'

NORMAL_NAME = 'T_MobWaterNormal'
FOAM_NAME = 'T_MobWaterFoam'

NORMAL_SIZE = 256
FOAM_SIZE = 256

# 8 doubling four times is 8, 16, 32, 64, 128 - all factors of 256.
# Few octaves and a coarse lattice. Five octaves puts detail down to a two-pixel lattice, and at any
# tiling large enough to cover a pond that becomes a shimmer the size of a pixel - which on a
# renderer with FXAA and no temporal filter is the worst thing that can be on a surface.
NORMAL_BASE_PERIOD = 4
NORMAL_OCTAVES = 3

# Coarser still. Foam is clumps, and clumps smaller than the foam band itself just erode the band.
FOAM_BASE_PERIOD = 4
FOAM_OCTAVES = 3

# How steep the generated surface is before its normal is taken. Higher is a rougher looking water
# without changing the frequency content, which is the dial that actually reads as wind.
NORMAL_STRENGTH = 0.9

# How much finer the lattice is across the wind than along it. 3 is a clear direction without the
# result reading as brushed metal, and it divides 256 at every octave.
NORMAL_STRETCH = 3


def _log(msg):
    unreal.log('[MobWater] ' + str(msg))


def _source_dir():
    """Where the PNGs are written before import. Intermediate, because they are not the asset."""
    plugin = unreal.Paths.project_plugins_dir() + 'Visuals/MobWater/Intermediate/TextureSource'
    directory = unreal.Paths.convert_relative_path_to_full(plugin)
    if not os.path.isdir(directory):
        os.makedirs(directory)
    return directory


# ---------------------------------------------------------------------------
# PNG
# ---------------------------------------------------------------------------

def _write_png(path, width, height, rows):
    """Eight bit RGBA, no interlacing. rows is a list of bytearrays, one per scanline."""
    raw = b''.join(b'\x00' + bytes(row) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', header)
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))

    with open(path, 'wb') as handle:
        handle.write(png)
    return path


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

def _hash(x, y, seed):
    n = (x * 374761393 + y * 668265263 + seed * 1274126177) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def _value_noise(u, v, period, seed):
    """Value noise on a square lattice that wraps at period, so the whole texture tiles."""
    return _value_noise2(u, v, period, period, seed)


def _value_noise2(u, v, period_x, period_y, seed):
    """Value noise on a lattice that can be finer in one axis than the other.

    Water is not isotropic. Wind stretches ripples across its own direction, so noise with the same
    frequency both ways reads as gravel or as beaten metal - never as a surface with something
    blowing over it. Both periods still have to divide the texture or the tile seams.
    """
    fx = u * period_x
    fy = v * period_y
    x0 = int(math.floor(fx))
    y0 = int(math.floor(fy))
    tx = fx - x0
    ty = fy - y0

    # Smoothstep rather than linear. Linear interpolation between lattice points leaves the lattice
    # visible as a grid of creases, which at these frequencies reads as woven cloth.
    sx = tx * tx * (3.0 - 2.0 * tx)
    sy = ty * ty * (3.0 - 2.0 * ty)

    x0 %= period_x
    y0 %= period_y
    x1 = (x0 + 1) % period_x
    y1 = (y0 + 1) % period_y

    a = _hash(x0, y0, seed)
    b = _hash(x1, y0, seed)
    c = _hash(x0, y1, seed)
    d = _hash(x1, y1, seed)

    top = a + (b - a) * sx
    bottom = c + (d - c) * sx
    return top + (bottom - top) * sy


def _fbm(u, v, seed, octaves, base_period, stretch=1):
    """Octaves of value noise. Stretch makes the lattice finer across v than along u.

    An integer, because both periods have to keep dividing the texture as they double.
    """
    total = 0.0
    weight = 0.0
    amplitude = 1.0
    period = base_period

    for octave in range(octaves):
        total += _value_noise2(u, v, period, period * stretch, seed + octave) * amplitude
        weight += amplitude
        amplitude *= 0.5
        period *= 2

    return total / weight if weight > 0.0 else 0.0


# ---------------------------------------------------------------------------
# The textures
# ---------------------------------------------------------------------------

def _normal_rows():
    """A tangent space normal map, from the gradient of a tiling height field.

    Central differences over the height rather than a noise per channel: a normal whose x and y are
    unrelated noises does not describe a surface, and lights it as though every texel faced a
    different way at random. This one is the derivative of something, so it shades like a shape.
    """
    size = NORMAL_SIZE
    step = 1.0 / size

    # Two fields crossed rather than one. A single stretched noise gives parallel corduroy; a second
    # one stretched the other way breaks it into the interlocking cells that water actually shows,
    # and being a product rather than a sum it keeps the troughs sharp and the crests separate.
    heights = []
    for y in range(size):
        row = []
        v = y / size
        for x in range(size):
            u = x / size
            along = _fbm(u, v, 11, NORMAL_OCTAVES, NORMAL_BASE_PERIOD, NORMAL_STRETCH)
            across = _fbm(v, u, 37, NORMAL_OCTAVES - 1, NORMAL_BASE_PERIOD, NORMAL_STRETCH)
            row.append(along * 0.65 + across * 0.35)
        heights.append(row)

    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            # Wrapped lookups, so the derivative tiles as cleanly as the height does.
            hl = heights[y][(x - 1) % size]
            hr = heights[y][(x + 1) % size]
            hd = heights[(y - 1) % size][x]
            hu = heights[(y + 1) % size][x]

            dx = (hr - hl) / (2.0 * step) * NORMAL_STRENGTH
            dy = (hu - hd) / (2.0 * step) * NORMAL_STRENGTH

            length = math.sqrt(dx * dx + dy * dy + 1.0)
            nx = -dx / length
            ny = -dy / length
            nz = 1.0 / length

            row.append(int(max(0.0, min(1.0, nx * 0.5 + 0.5)) * 255.0))
            row.append(int(max(0.0, min(1.0, ny * 0.5 + 0.5)) * 255.0))
            row.append(int(max(0.0, min(1.0, nz * 0.5 + 0.5)) * 255.0))
            row.append(255)
        rows.append(row)

    return rows


def _foam_rows():
    """Broken cellular froth: fbm, contrasted so it reads as clumps rather than as a cloud."""
    size = FOAM_SIZE
    rows = []

    for y in range(size):
        row = bytearray()
        for x in range(size):
            n = _fbm(x / size, y / size, 29, FOAM_OCTAVES, FOAM_BASE_PERIOD)

            # Pushed away from the middle, but gently. This only varies foam that is already there,
            # so heavy contrast here punches holes in a shoreline rather than making it interesting.
            n = max(0.0, min(1.0, (n - 0.45) * 1.8 + 0.5))
            value = int(n * 255.0)

            row.append(value)
            row.append(value)
            row.append(value)
            row.append(255)
        rows.append(row)

    return rows


def _import(filename, asset_name, compression):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', filename)
    task.set_editor_property('destination_path', TEX_ROOT)
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = unreal.load_asset('%s/%s' % (TEX_ROOT, asset_name))
    if texture is None:
        unreal.log_error('[MobWater] failed to import %s' % asset_name)
        return None

    texture.set_editor_property('compression_settings', compression)

    # Both are data rather than pictures. An sRGB curve on a normal bends the normal.
    texture.set_editor_property('srgb', False)
    texture.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property('address_y', unreal.TextureAddress.TA_WRAP)

    mob_water_version.stamp(texture)
    EAL.save_loaded_asset(texture, only_if_is_dirty=False)
    return texture


def build_normal():
    path = _write_png(os.path.join(_source_dir(), NORMAL_NAME + '.png'),
                      NORMAL_SIZE, NORMAL_SIZE, _normal_rows())
    texture = _import(path, NORMAL_NAME, unreal.TextureCompressionSettings.TC_NORMALMAP)
    _log('%s: %dx%d tiling' % (NORMAL_NAME, NORMAL_SIZE, NORMAL_SIZE))
    return texture


def build_foam():
    path = _write_png(os.path.join(_source_dir(), FOAM_NAME + '.png'),
                      FOAM_SIZE, FOAM_SIZE, _foam_rows())
    texture = _import(path, FOAM_NAME, unreal.TextureCompressionSettings.TC_MASKS)
    _log('%s: %dx%d tiling' % (FOAM_NAME, FOAM_SIZE, FOAM_SIZE))
    return texture


CAUSTICS_NAME = 'T_MobWaterCaustics'
CAUSTICS_SIZE = 256

# How many cells across the tile. Caustics are the web between cells, so this is directly how big a
# caustic cell is once the material has scaled it - and it has to divide the texture to tile.
CAUSTICS_CELLS = 8


def _cell_point(cx, cy, seed):
    """A jittered feature point inside a cell, wrapped so the whole lattice tiles."""
    x = _hash(cx % CAUSTICS_CELLS, cy % CAUSTICS_CELLS, seed)
    y = _hash(cx % CAUSTICS_CELLS, cy % CAUSTICS_CELLS, seed + 977)

    return (cx + 0.15 + x * 0.7, cy + 0.15 + y * 0.7)


def _caustics_rows():
    """Cellular noise, read as the web between cells rather than the cells themselves.

    Caustics are what a wavy surface does to parallel light: it focuses it into bright curves where
    the surface curvature converges. Those curves are the boundaries between catchment areas, which
    is exactly what the ridge between Worley cells is - so the second-nearest distance minus the
    nearest, sharpened, is a far better caustic than any amount of blurred noise.
    """
    size = CAUSTICS_SIZE
    rows = []

    for y in range(size):
        row = bytearray()
        fy = (y + 0.5) / size * CAUSTICS_CELLS

        for x in range(size):
            fx = (x + 0.5) / size * CAUSTICS_CELLS

            cx = int(math.floor(fx))
            cy = int(math.floor(fy))

            nearest = 1e9
            second = 1e9

            for oy in range(-1, 2):
                for ox in range(-1, 2):
                    px, py = _cell_point(cx + ox, cy + oy, 61)

                    dx = px - fx
                    dy = py - fy
                    distance = math.sqrt(dx * dx + dy * dy)

                    if distance < nearest:
                        second = nearest
                        nearest = distance
                    elif distance < second:
                        second = distance

            # The ridge: zero on a cell boundary, larger inside a cell. Inverted and sharpened it
            # becomes a thin bright web, which is what a caustic actually looks like.
            ridge = max(0.0, min(1.0, (second - nearest) * 1.6))
            web = (1.0 - ridge) ** 3.5

            value = int(max(0.0, min(1.0, web)) * 255.0)

            row.append(value)
            row.append(value)
            row.append(value)
            row.append(255)

        rows.append(row)

    return rows


def build_caustics():
    path = _write_png(os.path.join(_source_dir(), CAUSTICS_NAME + '.png'),
                      CAUSTICS_SIZE, CAUSTICS_SIZE, _caustics_rows())

    texture = _import(path, CAUSTICS_NAME, unreal.TextureCompressionSettings.TC_MASKS)
    _log('%s: %dx%d tiling, %d cells' % (CAUSTICS_NAME, CAUSTICS_SIZE, CAUSTICS_SIZE, CAUSTICS_CELLS))
    return texture


SKY_NAME = 'T_MobWaterSky'
SKY_WIDTH = 128
SKY_HEIGHT = 64


def _sky_rows():
    """A plain long-latitude sky, so water reflects something believable before anyone assigns an HDRI.

    Out of the box a project has no sky texture to give this, and a reflection of nothing is a flat
    grey sheen that reads as polished stone. A gradient with a pale horizon and a darker zenith is
    what the eye expects a lake to be showing it, and a real HDRI replaces it in one setting.

    Deliberately dull. It is a stand-in, and a stand-in that looks good enough to keep is one that
    ends up shipped.
    """
    rows = []

    for y in range(SKY_HEIGHT):
        row = bytearray()

        # v is 0 at the zenith, matching MobWaterLongLatUV.
        v = (y + 0.5) / SKY_HEIGHT
        horizon = 1.0 - abs(v - 0.5) * 2.0

        for _x in range(SKY_WIDTH):
            # Pale and warm at the horizon, deeper and cooler overhead. Below the horizon it keeps
            # going darker, which is what water reflects when it looks down at the far bank.
            haze = horizon ** 3.0

            r = 0.22 + 0.55 * haze
            g = 0.34 + 0.50 * haze
            b = 0.50 + 0.42 * haze

            if v > 0.5:
                # Under the horizon: ground rather than sky, so it stops being blue.
                ground = (v - 0.5) * 2.0
                r = r * (1.0 - ground) + 0.10 * ground
                g = g * (1.0 - ground) + 0.10 * ground
                b = b * (1.0 - ground) + 0.09 * ground

            row.append(int(max(0.0, min(1.0, r)) * 255.0))
            row.append(int(max(0.0, min(1.0, g)) * 255.0))
            row.append(int(max(0.0, min(1.0, b)) * 255.0))
            row.append(255)

        rows.append(row)

    return rows


def build_sky():
    path = _write_png(os.path.join(_source_dir(), SKY_NAME + '.png'),
                      SKY_WIDTH, SKY_HEIGHT, _sky_rows())

    texture = _import(path, SKY_NAME, unreal.TextureCompressionSettings.TC_DEFAULT)
    if texture:
        # A picture rather than data, unlike everything else here, so it keeps its sRGB curve. It
        # wraps in x because longitude does, and clamps in y because latitude does not.
        texture.set_editor_property('srgb', True)
        texture.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        mob_water_version.stamp(texture)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)

    _log('%s: %dx%d long-lat placeholder' % (SKY_NAME, SKY_WIDTH, SKY_HEIGHT))
    return texture


SPRITE_NAME = 'T_MobWaterSprite'
SPRITE_SIZE = 128

# Read against a lit viewport as often as a dark one, so the glyph is a light band inside a dark
# outline and neither background can swallow it.
SPRITE_LIGHT = (150, 214, 255)
SPRITE_DARK = (16, 58, 100)


def _sprite_rows():
    """The editor icon: three stacked waves, tapered at both ends."""
    size = SPRITE_SIZE
    rows = []

    lines = (0.30, 0.5, 0.70)
    amplitude = size * 0.065
    half = size * 0.045
    outline = size * 0.026

    for y in range(size):
        row = bytearray()
        for x in range(size):
            px = x + 0.5
            py = y + 0.5

            nearest = float(size)
            for line in lines:
                centre = line * size + amplitude * math.sin(px / size * 2.0 * math.pi)
                nearest = min(nearest, abs(py - centre))

            # Tapered rather than run to the border, so the icon reads as a mark and not as a bar.
            taper = min(1.0, min(px, size - px) / (size * 0.14))

            core = max(0.0, min(1.0, (half - nearest) + 0.5)) * taper
            alpha = max(0.0, min(1.0, (half + outline - nearest) + 0.5)) * taper

            for channel in range(3):
                value = SPRITE_DARK[channel] + (SPRITE_LIGHT[channel] - SPRITE_DARK[channel]) * core
                row.append(int(max(0.0, min(255.0, value))))
            row.append(int(alpha * 255.0))

        rows.append(row)

    return rows


def build_sprite():
    path = _write_png(os.path.join(_source_dir(), SPRITE_NAME + '.png'),
                      SPRITE_SIZE, SPRITE_SIZE, _sprite_rows())

    texture = _import(path, SPRITE_NAME, unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    if texture:
        # A picture, and one that is never tiled: it is drawn at one size in a viewport.
        texture.set_editor_property('srgb', True)
        texture.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        mob_water_version.stamp(texture)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)

    _log('%s: %dx%d icon' % (SPRITE_NAME, SPRITE_SIZE, SPRITE_SIZE))
    return texture


def build_all():
    build_normal()
    build_foam()
    build_sky()
    build_caustics()
    build_sprite()
    return True
