# Copyright (c) Jared Taylor. All Rights Reserved

"""Solves a Phillips sea offline and bakes it, so the ocean is a spectrum rather than eight sines.

Run from Water > Bake Ocean Spectrum, or:

    import mob_water_spectrum
    mob_water_spectrum.run()

What comes out is two textures and one data asset. The textures are atlases: every frame of the loop
laid out in a grid on one 2D texture, because a texture array or a volume on the mobile forward path
is a support question with no good answer on every device this has to run on, and a strip of a
hundred frames is four times the 2048 texels a side ES3.1 guarantees. Each cell is ringed by a copy
of its own opposite edge, so the hardware filter reaching past a cell reads the same water one tile
over instead of the same water a frame later.

It loops exactly, and that is worth the arithmetic it costs. Every component's angular frequency is
rounded down to a whole number of turns over the loop period before the transform runs, which makes
every phase factor an F-th root of unity - so the last frame really is the frame before the first,
there is no crossfade, and the inner loop has no transcendental in it at all.

The same bytes go to the CPU. A dedicated server has no texture, only a table, and a query that
answered out of a texture would answer differently on the machine that decides. Two implementations
of a Gerstner sum can be asserted equal; two solves of a Fourier transform in two languages cannot,
and the networking story needs the same answer rather than nearly the same one.

No third party dependency. numpy is used when the editor's Python has it and the transform is written
out longhand when it does not, because a bake that only runs on some machines is worse than one that
runs slowly on all of them. The editor's Python has no numpy, so longhand is the path that actually
runs, and at the shipped size it takes about six seconds - the twiddles are computed once and every
phase factor is a table read, which is what the exact loop buys back.
"""

import math
import os
import random
import struct
import zlib

import unreal

import mob_water_version

try:
    import numpy
except ImportError:
    numpy = None

EAL = unreal.EditorAssetLibrary

SPECTRUM_ROOT = '/MobWater/Spectra'

ASSET_NAME = 'SP_MobWater_Ocean'
DISPLACEMENT_NAME = 'T_MobWaterSpectrum'
NORMAL_NAME = 'T_MobWaterSpectrumNormal'

# Centimetres per second squared, matching MobWaterWaveConstants::Gravity. The dispersion relation
# is the same one the Gerstner tier uses, so the two tiers ride at the same speed.
GRAVITY = 980.0

# Rows of the opposite edge copied above and below each frame. Has to agree with
# MobWaterSpectrumConstants::Gutter and MOB_WATER_SPECTRUM_GUTTER.
GUTTER = 1

# ---------------------------------------------------------------------------
# The bake, and what it costs
# ---------------------------------------------------------------------------
#
# 64 x 64 over 128 frames, laid out sixteen frames across, is a 1056 x 528 atlas: 2.13 MB each, two
# of them, and another 2.00 MB of table in the package. Every one of these is a knob rather than a
# constant, and the three of them trade against each other rather than being independent.
#
# Frames buy time resolution and texels buy space resolution, and the sea decides which it wants. A
# wave's speed comes from its length, so the shortest wave the grid can hold is also the fastest, and
# a bake with more texels than frames strobes: the small chop teleports between frames instead of
# travelling. 64 texels across 61 m is a wave every 1.9 m at 1.1 s a cycle, and 128 frames over the
# 16 s loop samples that eight times a cycle. Doubling the resolution without doubling the frames
# would halve that and the surface would crawl.
#
# Anything finer belongs in the scrolling detail normals, which cost no memory and do not have to
# loop.
RESOLUTION = 64
FRAMES = 128

# How many frames sit across the atlas. Sixteen keeps both sides of a 128 frame bake near a thousand
# texels, which matters: ES3.1 only guarantees 2048 a side, and the whole reason the frames are in a
# grid rather than a column is that a column of them is four times that.
ATLAS_COLUMNS = 16

# How wide one tile of the field is. Large enough that the repeat is not the first thing seen from a
# clifftop, small enough that 64 texels still resolve a wave.
TILE_SIZE = 6144.0

# How long the loop takes.
#
# Short enough that the eye does not find the beat, long enough that the quantisation the loop costs
# is small: every component is rounded down to a whole number of turns over this, so a shorter period
# has fewer harmonics to round onto and the longest swell ends up travelling visibly slow.
LOOP_PERIOD = 16.0

# The wind this is solved for. It decides the shape of the spectrum and not its height - the height
# is TARGET_RMS_HEIGHT below - so this is really the question of which wavelength carries most of the
# sea. Phillips peaks at V squared over twice g, and a peak longer than the tile means the tile holds
# only the tail of the spectrum and the result reads as chop with no swell under it. Six metres a
# second peaks at 33 m, which is half of this tile.
WIND_SPEED = 600.0
WIND_DIRECTION = 30.0

# Root mean square of the baked height. Significant wave height, which is what a sea state is quoted
# as, is about four of these - so 35 cm here is a metre and a half of swell.
TARGET_RMS_HEIGHT = 35.0

# How far the transform is allowed to pull points towards a crest. 0 leaves rounded swell; much above
# 1 and the surface starts to cross itself, which the foam term reports rather than hides.
CHOPPINESS = 1.0

# How hard waves running against the wind are suppressed. Not zero: a sea with no component against
# the wind reads as a corrugated sheet rather than as water.
AGAINST_WIND = 0.07

# Where the spectrum is cut off, as a fraction of a texel.
#
# Not a decoration. The grid cannot carry a wave shorter than two texels, and energy left down there
# does not disappear - it bakes as noise that the loop then strobes. 0.6 of a texel puts the knee
# just above two and a half texels, which is the shortest wave the frame count can also carry.
SMALL_WAVE_TEXELS = 0.6

SEED = 20260817


def _log(msg):
    unreal.log('[MobWater] ' + str(msg))


def _source_dir():
    """Where the atlases and the table are written before import."""
    plugin = unreal.Paths.project_plugins_dir() + 'Visuals/MobWater/Intermediate/SpectrumSource'
    directory = unreal.Paths.convert_relative_path_to_full(plugin)
    if not os.path.isdir(directory):
        os.makedirs(directory)
    return directory


# ---------------------------------------------------------------------------
# The transform
# ---------------------------------------------------------------------------

def _bit_reverse(n):
    """The permutation an iterative radix-2 transform reads its input in."""
    bits = n.bit_length() - 1
    order = [0] * n
    for i in range(n):
        r = 0
        v = i
        for _ in range(bits):
            r = (r << 1) | (v & 1)
            v >>= 1
        order[i] = r
    return order


class _Plan(object):
    """Everything a length that does not change needs computed once.

    Rebuilding the twiddles inside the transform is most of the cost of a longhand transform, and the
    bake runs a couple of hundred of them at one length.
    """

    def __init__(self, n):
        self.n = n
        self.order = _bit_reverse(n)

        self.stages = []
        size = 2
        while size <= n:
            half = size // 2
            step = 2.0 * math.pi / size
            self.stages.append((size, half, [complex(math.cos(step * j), math.sin(step * j))
                                             for j in range(half)]))
            size *= 2


def _fft(values, plan):
    """One length-n transform with the positive sign, unnormalised. In place on a new list."""
    n = plan.n
    out = [values[i] for i in plan.order]

    for size, half, twiddles in plan.stages:
        for start in range(0, n, size):
            for j in range(half):
                a = out[start + j]
                b = out[start + j + half] * twiddles[j]
                out[start + j] = a + b
                out[start + j + half] = a - b

    return out


def _ifft2_real(grid, plan):
    """The real part of a 2D transform of a Hermitian grid, unnormalised.

    grid is a flat list of complex in row major order, indexed [y * n + x] with the wave numbers in
    the transform's own order - index 0 is zero, and the upper half is negative.
    """
    n = plan.n

    rows = []
    for y in range(n):
        rows.append(_fft(grid[y * n:(y + 1) * n], plan))

    out = [0.0] * (n * n)
    column = [0j] * n
    for x in range(n):
        for y in range(n):
            column[y] = rows[y][x]
        transformed = _fft(column, plan)
        for y in range(n):
            out[y * n + x] = transformed[y].real

    return out


def _ifft2_real_numpy(grid, n):
    """The same thing, when the editor's Python happens to have numpy."""
    array = numpy.asarray(grid, dtype=numpy.complex128).reshape((n, n))

    # numpy's inverse divides by the element count and takes the negative sign, so the forward
    # transform scaled back up is the sum this wants.
    # numpy's forward transform takes the negative sign, and this sum takes the positive one. The
    # real part of the forward transform of the conjugate is the same number without a second pass.
    return numpy.real(numpy.fft.fft2(numpy.conj(array))).ravel().tolist()


# ---------------------------------------------------------------------------
# The spectrum
# ---------------------------------------------------------------------------

def _phillips(kx, ky, wind_x, wind_y, wind_speed, small_wave):
    """Phillips, with the two corrections nobody can leave out.

    The first is the suppression of waves shorter than a chosen length: without it the spectrum has
    energy right down to the grid and the surface bakes as noise. The second is the directional term,
    with waves running against the wind kept rather than removed - a sea travelling one way only
    reads as corrugation.
    """
    k_squared = kx * kx + ky * ky
    if k_squared < 1e-12:
        return 0.0

    k = math.sqrt(k_squared)

    largest = wind_speed * wind_speed / GRAVITY
    if largest <= 0.0:
        return 0.0

    damped = -1.0 / (k_squared * largest * largest)
    if damped < -30.0:
        return 0.0

    aligned = (kx * wind_x + ky * wind_y) / k
    directional = aligned * aligned
    if aligned < 0.0:
        directional *= AGAINST_WIND

    return math.exp(damped) / (k_squared * k_squared) * directional * math.exp(-k_squared * small_wave * small_wave)


def _signed(index, n):
    """The wave number an array index stands for. The upper half of the array is negative."""
    return index if index < n // 2 else index - n


def _build_fields(resolution, frames, tile, period, wind_speed, wind_direction, choppiness, seed):
    """Every frame of the loop, as displacement and folding.

    Returns (frames_of_texels, rms_height, max_horizontal, max_vertical, max_slope), where each frame
    is a flat list of (dx, dy, dz, fold) in row major order.
    """
    n = resolution

    wind_radians = math.radians(wind_direction)
    wind_x = math.cos(wind_radians)
    wind_y = math.sin(wind_radians)

    # Shorter than two texels is energy the grid cannot carry, and leaving it in bakes as noise that
    # the loop then strobes rather than as detail.
    small_wave = tile / float(n) * SMALL_WAVE_TEXELS

    rng = random.Random(seed)

    fundamental = 2.0 * math.pi / tile
    turn = 2.0 * math.pi / period

    count = n * n

    # The stationary half of every component, and the multipliers each derived field needs. Built once
    # because none of it depends on time - only the phase factor does.
    h0 = [0j] * count
    harmonic = [0] * count

    kx_of = [0.0] * count
    ky_of = [0.0] * count
    k_of = [0.0] * count

    for y in range(n):
        for x in range(n):
            index = y * n + x

            kx = fundamental * _signed(x, n)
            ky = fundamental * _signed(y, n)

            kx_of[index] = kx
            ky_of[index] = ky
            k_of[index] = math.sqrt(kx * kx + ky * ky)

            power = _phillips(kx, ky, wind_x, wind_y, wind_speed, small_wave)
            amplitude = math.sqrt(power * 0.5)

            h0[index] = complex(rng.gauss(0.0, 1.0) * amplitude, rng.gauss(0.0, 1.0) * amplitude)

            # Rounded down to a whole number of turns over the loop period. This is the one line that
            # makes the result loop: every phase factor below is then an exact root of unity, so the
            # frame after the last is the first and nothing has to be crossfaded.
            harmonic[index] = int(math.sqrt(GRAVITY * k_of[index]) / turn) if k_of[index] > 0.0 else 0

    # The conjugate half, read from the opposite wave number so the field comes out real.
    h0_conj = [0j] * count
    for y in range(n):
        for x in range(n):
            h0_conj[y * n + x] = h0[((n - y) % n) * n + ((n - x) % n)].conjugate()

    # The frames' worth of roots of unity, so the inner loop is a table read.
    roots = [complex(math.cos(2.0 * math.pi * j / frames), math.sin(2.0 * math.pi * j / frames))
             for j in range(frames)]

    plan = None if numpy else _Plan(n)

    def transform(grid):
        return _ifft2_real_numpy(grid, n) if numpy else _ifft2_real(grid, plan)

    raw = []
    sum_squares = 0.0

    for frame in range(frames):
        spectrum = [0j] * count

        for index in range(count):
            root = roots[(harmonic[index] * frame) % frames]
            spectrum[index] = h0[index] * root + h0_conj[index] * root.conjugate()

        height = transform(spectrum)

        # Points are pulled towards a crest rather than away from one, which is the sign that sharpens
        # the crest and flattens the trough. The other sign is the sea upside down and reads as it.
        disp_x = transform([1j * (kx_of[i] / k_of[i]) * spectrum[i] if k_of[i] > 0.0 else 0j
                            for i in range(count)])
        disp_y = transform([1j * (ky_of[i] / k_of[i]) * spectrum[i] if k_of[i] > 0.0 else 0j
                            for i in range(count)])

        # The Jacobian of that pull. Where it drops below one the surface is compressing, which is
        # where water actually breaks white - so foam lands on the crests that are steep rather than
        # on every crest.
        jxx = transform([-(kx_of[i] * kx_of[i] / k_of[i]) * spectrum[i] if k_of[i] > 0.0 else 0j
                         for i in range(count)])
        jyy = transform([-(ky_of[i] * ky_of[i] / k_of[i]) * spectrum[i] if k_of[i] > 0.0 else 0j
                         for i in range(count)])
        jxy = transform([-(kx_of[i] * ky_of[i] / k_of[i]) * spectrum[i] if k_of[i] > 0.0 else 0j
                         for i in range(count)])

        raw.append((height, disp_x, disp_y, jxx, jyy, jxy))

        for value in height:
            sum_squares += value * value

        if (frame + 1) % 16 == 0 or frame + 1 == frames:
            _log('  frame %d of %d' % (frame + 1, frames))

    measured = math.sqrt(sum_squares / float(count * frames)) if count else 0.0
    scale = (TARGET_RMS_HEIGHT / measured) if measured > 1e-9 else 0.0

    out = []
    max_horizontal = 1e-4
    max_vertical = 1e-4

    for height, disp_x, disp_y, jxx, jyy, jxy in raw:
        texels = []
        for index in range(count):
            dz = height[index] * scale
            dx = disp_x[index] * scale * choppiness
            dy = disp_y[index] * scale * choppiness

            fold_xx = 1.0 + jxx[index] * scale * choppiness
            fold_yy = 1.0 + jyy[index] * scale * choppiness
            fold_xy = jxy[index] * scale * choppiness

            jacobian = fold_xx * fold_yy - fold_xy * fold_xy
            fold = min(max(1.0 - jacobian, 0.0), 1.0)

            texels.append((dx, dy, dz, fold))

            max_horizontal = max(max_horizontal, abs(dx), abs(dy))
            max_vertical = max(max_vertical, abs(dz))

        out.append(texels)

    # The slope every frame reaches, measured across one texel of the baked height - which is the same
    # difference the query takes, so the two read the same surface.
    step = tile / float(n)
    max_slope = 1e-4
    for texels in out:
        for y in range(n):
            for x in range(n):
                east = texels[y * n + (x + 1) % n][2]
                west = texels[y * n + (x - 1) % n][2]
                north = texels[((y + 1) % n) * n + x][2]
                south = texels[((y - 1) % n) * n + x][2]

                max_slope = max(max_slope,
                                abs(east - west) / (2.0 * step),
                                abs(north - south) / (2.0 * step))

    return out, measured * scale, max_horizontal, max_vertical, max_slope


# ---------------------------------------------------------------------------
# What comes out
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
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))

    with open(path, 'wb') as handle:
        handle.write(png)
    return path


def _byte(value):
    return min(max(int(round(value * 255.0)), 0), 255)


def atlas_size(resolution, frames, columns):
    """How large the atlas holding this many frames is, in texels."""
    cell = resolution + 2 * GUTTER
    rows = (frames + columns - 1) // columns
    return columns * cell, rows * cell


def _atlas_rows(frames_of_texels, resolution, columns, encode):
    """The frames laid out in a grid, each ringed by a copy of its own opposite edge.

    encode turns one texel into four bytes. The gutter is a copy of a texel that is already in the
    frame rather than a fade to anything, so the filter reaching past a cell's edge gets exactly what
    wrapping the field would have given it - which is the whole reason one bilinear tap can address a
    tiling field out of a texture that does not tile.

    Cells past the last frame are left black. They are never addressed: the frame index is folded by
    the frame count before it becomes a cell.
    """
    cell = resolution + 2 * GUTTER
    width, height = atlas_size(resolution, len(frames_of_texels), columns)

    rows = [bytearray(width * 4) for _ in range(height)]

    for frame, texels in enumerate(frames_of_texels):
        # A cell's texel, in the field's own coordinates, with the gutter reading round the edge.
        cell_rows = []
        for row in range(cell):
            y = (row - GUTTER) % resolution
            data = bytearray()
            for column in range(cell):
                x = (column - GUTTER) % resolution
                data += encode(texels[y * resolution + x], x, y, texels)
            cell_rows.append(data)

        origin_x = (frame % columns) * cell * 4
        origin_y = (frame // columns) * cell

        for row in range(cell):
            rows[origin_y + row][origin_x:origin_x + cell * 4] = cell_rows[row]

    return rows


def _import(filename, asset_name):
    task = unreal.AssetImportTask()

    # Named rather than left to the file extension: AssetTools only routes an import through
    # Interchange when no factory was given, and Interchange's import deadlocks a live editor.
    task.set_editor_property('factory', unreal.TextureFactory())

    task.set_editor_property('filename', filename)
    task.set_editor_property('destination_path', SPECTRUM_ROOT)
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = unreal.load_asset('%s/%s' % (SPECTRUM_ROOT, asset_name))
    if texture is None:
        unreal.log_error('[MobWater] failed to import %s' % asset_name)
        return None

    # Uncompressed, and that is not negotiable. Every block format quantises the three channels
    # together, so a compressed displacement map moves a vertex to wherever the block's average said,
    # and the tiling seam that produces is a straight line across the whole ocean.
    texture.set_editor_property('compression_settings',
                                unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property('srgb', False)

    # No mips. The frames sit side by side, so the second mip averages one frame with the next and
    # every mip past it is more of that.
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)

    # Clamped on both axes. What wraps is the field, not the atlas, and the gutter around each cell is
    # what stands in for wrapping - so the sampler must never do it as well.
    texture.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)

    texture.set_editor_property('never_stream', True)

    mob_water_version.stamp(texture)
    EAL.save_loaded_asset(texture, only_if_is_dirty=False)
    return texture


def build(resolution=RESOLUTION, frames=FRAMES, tile=TILE_SIZE, period=LOOP_PERIOD,
          wind_speed=WIND_SPEED, wind_direction=WIND_DIRECTION, choppiness=CHOPPINESS, seed=SEED,
          columns=ATLAS_COLUMNS):
    """Solves the sea and writes everything it becomes."""
    if resolution & (resolution - 1):
        raise ValueError('resolution has to be a power of two, not %d' % resolution)

    width, height = atlas_size(resolution, frames, columns)
    if width > 2048 or height > 2048:
        raise ValueError('a %dx%d atlas is past what ES3.1 guarantees a texture may be. Fewer frames, '
                         'a smaller resolution, or a squarer layout.' % (width, height))

    _log('Baking %dx%d over %d frames%s' % (resolution, resolution, frames,
                                            '' if numpy else ' (no numpy, longhand transform)'))

    fields, rms, max_horizontal, max_vertical, max_slope = _build_fields(
        resolution, frames, tile, period, wind_speed, wind_direction, choppiness, seed)

    # Rounded up, so the encoding never clips and the number on the asset is one a person can read.
    horizontal_scale = math.ceil(max_horizontal)
    vertical_scale = math.ceil(max_vertical)
    normal_scale = math.ceil(max_slope * 100.0) / 100.0

    directory = _source_dir()

    def encode_displacement(texel, _x, _y, _texels):
        dx, dy, dz, fold = texel
        return bytes((
            _byte(dx / horizontal_scale * 0.5 + 0.5),
            _byte(dy / horizontal_scale * 0.5 + 0.5),
            _byte(dz / vertical_scale * 0.5 + 0.5),
            _byte(fold)))

    step = tile / float(resolution)

    def encode_normal(texel, x, y, texels):
        east = texels[y * resolution + (x + 1) % resolution][2]
        west = texels[y * resolution + (x - 1) % resolution][2]
        north = texels[((y + 1) % resolution) * resolution + x][2]
        south = texels[((y - 1) % resolution) * resolution + x][2]

        # The negated slope, so the shader reconstructs a normal with a normalize and no minus signs
        # left over to get the wrong way round.
        slope_x = -(east - west) / (2.0 * step)
        slope_y = -(north - south) / (2.0 * step)

        return bytes((
            _byte(slope_x / normal_scale * 0.5 + 0.5),
            _byte(slope_y / normal_scale * 0.5 + 0.5),
            _byte(texel[3]),
            255))

    displacement_png = _write_png(
        os.path.join(directory, DISPLACEMENT_NAME + '.png'), width, height,
        _atlas_rows(fields, resolution, columns, encode_displacement))

    normal_png = _write_png(
        os.path.join(directory, NORMAL_NAME + '.png'), width, height,
        _atlas_rows(fields, resolution, columns, encode_normal))

    # The table the query reads: the same texels the atlas holds, without its gutter. Written as a
    # file rather than handed over as a list, because two million Python integers crossing the binding
    # costs minutes and a gigabyte to move bytes that are already on disk.
    table_path = os.path.join(directory, ASSET_NAME + '.bin')
    table = bytearray()
    for texels in fields:
        for texel in texels:
            table += encode_displacement(texel, 0, 0, texels)

    with open(table_path, 'wb') as handle:
        handle.write(table)

    displacement = _import(displacement_png, DISPLACEMENT_NAME)
    normal = _import(normal_png, NORMAL_NAME)

    asset = unreal.load_asset('%s/%s' % (SPECTRUM_ROOT, ASSET_NAME))
    if asset is None:
        # The class is named on the factory as well as on the create call. Left unset the factory
        # opens a picker, which in an automated run is a modal dialog nobody is there to answer.
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.MobWaterSpectrum)

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ASSET_NAME, SPECTRUM_ROOT, unreal.MobWaterSpectrum, factory)

    asset.set_editor_property('displacement_texture', displacement)
    asset.set_editor_property('normal_texture', normal)
    asset.set_editor_property('tile_size', tile)
    asset.set_editor_property('loop_period', period)
    asset.set_editor_property('resolution', resolution)
    asset.set_editor_property('frames', frames)
    asset.set_editor_property('atlas_columns', columns)
    asset.set_editor_property('horizontal_scale', horizontal_scale)
    asset.set_editor_property('vertical_scale', vertical_scale)
    asset.set_editor_property('normal_scale', normal_scale)
    asset.record_bake(wind_speed, wind_direction, choppiness, rms, seed)

    if not asset.load_samples_from_file(table_path):
        raise RuntimeError('the spectrum table did not load; the query would answer a flat sea')

    mob_water_version.stamp(asset)
    EAL.save_loaded_asset(asset, only_if_is_dirty=False)

    atlas_mb = width * height * 4 / (1024.0 * 1024.0)
    table_mb = len(table) / (1024.0 * 1024.0)

    _log('%s: %.0f cm tile, %.1f s loop, %.1f cm rms (about %.1f m significant)'
         % (ASSET_NAME, tile, period, rms, rms * 4.0 / 100.0))
    _log('  %dx%d atlas, %.2f MB each and two of them, and %.2f MB of table'
         % (width, height, atlas_mb, table_mb))
    _log('  displacement +/- %.0f cm across, +/- %.0f cm up, slope to %.2f'
         % (horizontal_scale, vertical_scale, normal_scale))

    return asset


def run():
    build()
    _log('Done.')
    return True


if __name__ == '__main__':
    run()
