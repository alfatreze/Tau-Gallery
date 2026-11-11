"""TGY reconstruction, reference (docs/TGY_RECONSTRUCTION_V0.md): from quantised coefficients to RGB, defined to equal what libjpeg-turbo produces for the same coefficients (islow IDCT, h2v2 fancy upsampling, the integer
YCbCr to RGB tables), so a picture shows the same pixels as on a PC. Integer arithmetic only, numpy for speed. Also: splitting a picture's coefficient blocks into 128 x 128 tiles (partial tiles at the right and bottom edges
keep only the blocks that exist) and putting them back, so the whole JPEG -> TGY tiles -> pixels chain can be checked end to end."""
import numpy as np
import jpegcoef

CONST_BITS, PASS1_BITS = 13, 2
F = dict(f0298=2446, f0390=3196, f0541=4433, f0765=6270, f0899=7373, f1175=9633, f1501=12299, f1847=15137, f1961=16069, f2053=16819, f2562=20995, f3072=25172)

def _descale(x, n): return (x + (1 << (n - 1))) >> n

def _range_limit_post_idct(d):
    """libjpeg's post-IDCT range limit: d (the descaled value, before the +128 level shift) is masked to 10 bits and looked up: [0,127] -> d+128, [128,511] -> 255, [512,895] -> 0, [896,1023] -> d-896."""
    j = d & 1023
    return np.where(j < 128, j + 128, np.where(j < 512, 255, np.where(j < 896, 0, j - 896))).astype(np.uint8)

def idct_islow(c):
    """c: int64 array [n, 8, 8] of DEQUANTISED coefficients in natural order (row = vertical frequency). Returns uint8 [n, 8, 8] samples: libjpeg's jpeg_idct_islow (jidctint.c), column pass then row pass."""
    c = c.astype(np.int64); ws = np.zeros_like(c)
    def butterfly(i0, i1, i2, i3, i4, i5, i6, i7):
        z1 = (i2 + i6) * F["f0541"]; t2 = z1 + i6 * (-F["f1847"]); t3 = z1 + i2 * F["f0765"]
        t0 = (i0 + i4) << CONST_BITS; t1 = (i0 - i4) << CONST_BITS
        t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2
        a0, a1, a2, a3 = i7, i5, i3, i1
        z1 = a0 + a3; z2 = a1 + a2; z3 = a0 + a2; z4 = a1 + a3; z5 = (z3 + z4) * F["f1175"]
        a0 = a0 * F["f0298"]; a1 = a1 * F["f2053"]; a2 = a2 * F["f3072"]; a3 = a3 * F["f1501"]
        z1 = z1 * (-F["f0899"]); z2 = z2 * (-F["f2562"]); z3 = z3 * (-F["f1961"]); z4 = z4 * (-F["f0390"]); z3 = z3 + z5; z4 = z4 + z5
        a0 = a0 + z1 + z3; a1 = a1 + z2 + z4; a2 = a2 + z2 + z3; a3 = a3 + z1 + z4
        return [t10 + a3, t11 + a2, t12 + a1, t13 + a0, t13 - a0, t12 - a1, t11 - a2, t10 - a3]            # outputs 0, 1, 2, 3, 4, 5, 6, 7
    col = butterfly(*[c[:, r, :] for r in range(8)])                                                    # pass 1: down the columns (the 8 inputs of a column are rows 0..7)
    for r in range(8): ws[:, r, :] = _descale(col[r], CONST_BITS - PASS1_BITS)
    row = butterfly(*[ws[:, :, k] for k in range(8)])                                                   # pass 2: along the rows
    out = np.zeros(c.shape, dtype=np.uint8)
    for k in range(8): out[:, :, k] = _range_limit_post_idct(_descale(row[k], CONST_BITS + PASS1_BITS + 3))
    return out

def component_plane(comp, qzz, true_w_samples, true_h_samples):
    """The component's samples: dequantise, IDCT every block, assemble the blocks, crop to the component's true size (ceil(width * h / hmax) by ceil(height * v / vmax)) - the samples libjpeg keeps."""
    q = np.zeros(64, dtype=np.int64); q[jpegcoef.ZZ] = qzz
    nat = jpegcoef.natural(comp.coef.astype(np.int64)) * q
    px = idct_islow(nat.reshape(-1, 8, 8).transpose(0, 1, 2).reshape(-1, 8, 8)).reshape(comp.blocks_h, comp.blocks_w, 8, 8)
    plane = px.transpose(0, 2, 1, 3).reshape(comp.blocks_h * 8, comp.blocks_w * 8)
    return plane[:true_h_samples, :true_w_samples]

def upsample_h2v2_fancy(p):
    """libjpeg's h2v2_fancy_upsample on a whole plane (jdsample.c): triangle filter 9/16, 3/16, 3/16, 1/16, the first and last real rows and columns replicated; for a plane no wider than 2 samples libjpeg uses the box filter instead."""
    h, w = p.shape
    if w <= 2: return np.repeat(np.repeat(p, 2, axis=0), 2, axis=1)
    p = p.astype(np.int64); out = np.zeros((2 * h, 2 * w), dtype=np.int64)
    up = np.vstack([p[:1], p[:-1]]); down = np.vstack([p[1:], p[-1:]])                                   # the nearest other row: above for the first output row, below for the second
    for v, other in ((0, up), (1, down)):
        s = 3 * p + other                                                                                 # vertical weights 3 : 1, per column
        o = np.zeros((h, 2 * w), dtype=np.int64)
        o[:, 0] = (s[:, 0] * 4 + 8) >> 4; o[:, 1] = (s[:, 0] * 3 + s[:, 1] + 7) >> 4
        o[:, 2:2 * w - 2:2] = (s[:, 1:-1] * 3 + s[:, :-2] + 8) >> 4; o[:, 3:2 * w - 2:2] = (s[:, 1:-1] * 3 + s[:, 2:] + 7) >> 4
        o[:, 2 * w - 2] = (s[:, -1] * 3 + s[:, -2] + 8) >> 4; o[:, 2 * w - 1] = (s[:, -1] * 4 + 7) >> 4
        out[v::2, :] = o
    return out.astype(np.uint8)

def upsample_h2v2_tile(p, tile=64):
    """The TGY profile: the same filter applied to each tile's chroma samples on their own (a chroma tile is 64 x 64 samples, smaller at the right and bottom edges of the picture), the tile's edge samples replicated, so a tile
    never needs a sample of its neighbour. Differs from libjpeg's whole-plane result only in the outermost row and column of luma samples of each tile (measured: tools/gallery/measure_tile_upsampling.py)."""
    h, w = p.shape; out = np.zeros((2 * h, 2 * w), dtype=np.uint8)
    for y in range(0, h, tile):
        for x in range(0, w, tile): out[2 * y:2 * min(y + tile, h), 2 * x:2 * min(x + tile, w)] = upsample_h2v2_fancy(p[y:y + tile, x:x + tile])
    return out

def _tabs():
    SCALE = 16; ONE_HALF = 1 << (SCALE - 1); fix = lambda x: int(x * (1 << SCALE) + 0.5)
    x = np.arange(256) - 128
    return ((fix(1.40200) * x + ONE_HALF) >> SCALE, (fix(1.77200) * x + ONE_HALF) >> SCALE, (-fix(0.71414)) * x, (-fix(0.34414)) * x + ONE_HALF)
CR_R, CB_B, CR_G, CB_G = _tabs()

def ycc_to_rgb(y, cb, cr):
    """libjpeg's jdcolor.c integer conversion (16-bit fixed point tables), clamped to 0..255."""
    y = y.astype(np.int64); cb = cb.astype(np.int64); cr = cr.astype(np.int64)
    r = y + CR_R[cr]; g = y + ((CB_G[cb] + CR_G[cr]) >> 16); b = y + CB_B[cb]
    return np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8)

def decode_pixels(j, upsample=upsample_h2v2_fancy):
    """The picture's RGB (height, width, 3) from a jpegcoef.Jpeg: what libjpeg-turbo gives for these coefficients (4:4:4 and 4:2:0 and grey)."""
    hmax = max(c.h for c in j.components); vmax = max(c.v for c in j.components); planes = []
    for c in j.components:
        tw = -(-j.width * c.h // hmax); th = -(-j.height * c.v // vmax); p = component_plane(c, j.quant[c.tq], tw, th)
        if (c.h, c.v) != (hmax, vmax):
            if (hmax // c.h, vmax // c.v) != (2, 2): raise ValueError("only 4:4:4 and 4:2:0")
            p = upsample(p)
        planes.append(p[:j.height, :j.width])
    if len(planes) == 1: return np.stack([planes[0]] * 3, axis=-1)
    return ycc_to_rgb(*planes)

def decode_pixels_tgy(j):
    """The normative TGY reconstruction: decode_pixels with the tile-local chroma upsampling."""
    return decode_pixels(j, upsample=upsample_h2v2_tile)

TILE = 128
def split_tiles(j):
    """The picture's blocks as tiles of 128 x 128 luma samples: a list of (ty, tx, grids, shapes) in raster order; a tile at the right or bottom edge keeps only the blocks that exist in each component's (MCU padded) grid."""
    hmax = max(c.h for c in j.components); vmax = max(c.v for c in j.components)
    per = [(TILE // 8 * c.v // vmax, TILE // 8 * c.h // hmax) for c in j.components]; tys = -(-j.components[0].blocks_h // per[0][0]); txs = -(-j.components[0].blocks_w // per[0][1]); out = []
    for ty in range(tys):
        for tx in range(txs):
            grids, shapes = [], []
            for c, (pr, pc) in zip(j.components, per):
                r0, c0 = ty * pr, tx * pc; rows = min(pr, c.blocks_h - r0); cols = min(pc, c.blocks_w - c0)
                grids.append([[c.coef[r0 + y, c0 + x].tolist() for x in range(cols)] for y in range(rows)]); shapes.append((rows, cols))
            out.append((ty, tx, grids, shapes))
    return out

def assemble(j, tiles):
    """Puts decoded tiles back into the picture's coefficient arrays (j's components are filled in place, j gives the geometry)."""
    hmax = max(c.h for c in j.components); vmax = max(c.v for c in j.components)
    per = [(TILE // 8 * c.v // vmax, TILE // 8 * c.h // hmax) for c in j.components]
    for c in j.components: c.coef[:] = 0
    for ty, tx, grids, shapes in tiles:
        for c, (pr, pc), g in zip(j.components, per, grids):
            for y, row in enumerate(g):
                for x, blk in enumerate(row): c.coef[ty * pr + y, tx * pc + x] = blk
    return j
