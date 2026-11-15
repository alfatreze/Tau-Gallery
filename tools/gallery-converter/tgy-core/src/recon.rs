//! Reconstruction: quantised coefficients to RGB, equal to what libjpeg-turbo gives for the same coefficients (islow IDCT, fancy upsampling,
//! the integer YCbCr to RGB tables). A port of `tools/gallery/tgy_recon.py` (checked against it), with the zero-column and zero-row shortcuts of
//! jidctint.c (which give identical values). Also the split of a picture into 128 x 128 luma tiles for the symbol layer.
use crate::jpegcoef::{Comp, Jpeg};
use crate::v0::{Block, Grid, ZZN};
use rayon::prelude::*;

const CONST_BITS: i64 = 13;
const PASS1_BITS: i64 = 2;
const F0298: i64 = 2446;
const F0390: i64 = 3196;
const F0541: i64 = 4433;
const F0765: i64 = 6270;
const F0899: i64 = 7373;
const F1175: i64 = 9633;
const F1501: i64 = 12299;
const F1847: i64 = 15137;
const F1961: i64 = 16069;
const F2053: i64 = 16819;
const F2562: i64 = 20995;
const F3072: i64 = 25172;

#[inline]
fn descale(x: i64, n: i64) -> i64 { (x + (1 << (n - 1))) >> n }

#[inline]
fn range_limit(d: i64) -> u8 {
    let j = d & 1023;
    if j < 128 { (j + 128) as u8 } else if j < 512 { 255 } else if j < 896 { 0 } else { (j - 896) as u8 }
}

#[inline]
fn butterfly(i: [i64; 8]) -> [i64; 8] {
    let [i0, i1, i2, i3, i4, i5, i6, i7] = i;
    let z1 = (i2 + i6) * F0541;
    let t2 = z1 + i6 * (-F1847);
    let t3 = z1 + i2 * F0765;
    let t0 = (i0 + i4) << CONST_BITS;
    let t1 = (i0 - i4) << CONST_BITS;
    let (t10, t13, t11, t12) = (t0 + t3, t0 - t3, t1 + t2, t1 - t2);
    let (mut a0, mut a1, mut a2, mut a3) = (i7, i5, i3, i1);
    let mut z1 = a0 + a3;
    let mut z2 = a1 + a2;
    let mut z3 = a0 + a2;
    let mut z4 = a1 + a3;
    let z5 = (z3 + z4) * F1175;
    a0 *= F0298;
    a1 *= F2053;
    a2 *= F3072;
    a3 *= F1501;
    z1 *= -F0899;
    z2 *= -F2562;
    z3 *= -F1961;
    z4 *= -F0390;
    z3 += z5;
    z4 += z5;
    a0 += z1 + z3;
    a1 += z2 + z4;
    a2 += z2 + z3;
    a3 += z1 + z4;
    [t10 + a3, t11 + a2, t12 + a1, t13 + a0, t13 - a0, t12 - a1, t11 - a2, t10 - a3]
}

/// libjpeg's jpeg_idct_islow on one block of DEQUANTISED coefficients in natural order (row = vertical frequency); 64 samples, row-major.
pub fn idct_islow(c: &[i64; 64], out: &mut [u8]) {
    let mut ws = [0i64; 64];
    for col in 0..8 {
        if (1..8).all(|r| c[r * 8 + col] == 0) {
            let dc = c[col] << PASS1_BITS;
            for r in 0..8 { ws[r * 8 + col] = dc; }
        } else {
            let o = butterfly([c[col], c[8 + col], c[16 + col], c[24 + col], c[32 + col], c[40 + col], c[48 + col], c[56 + col]]);
            for r in 0..8 { ws[r * 8 + col] = descale(o[r], CONST_BITS - PASS1_BITS); }
        }
    }
    for r in 0..8 {
        let w = &ws[r * 8..r * 8 + 8];
        if w[1..].iter().all(|&v| v == 0) {
            let v = range_limit(descale(w[0], PASS1_BITS + 3));
            for k in 0..8 { out[r * 8 + k] = v; }
        } else {
            let o = butterfly([w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]]);
            for k in 0..8 { out[r * 8 + k] = range_limit(descale(o[k], CONST_BITS + PASS1_BITS + 3)); }
        }
    }
}

/// A component's samples (dequantise, IDCT every block, assemble), cropped to tw x th. Row-major, stride tw.
pub fn component_plane(comp: &Comp, qzz: &[u16; 64], tw: usize, th: usize) -> Vec<u8> {
    let mut q = [0i64; 64];
    for k in 0..64 { q[ZZN[k]] = qzz[k] as i64; }
    let (bw, bh) = (comp.blocks_w, comp.blocks_h);
    let stride = bw * 8;
    let mut plane = vec![0u8; stride * bh * 8];
    plane.par_chunks_mut(stride * 8).enumerate().for_each(|(by, rows)| {
        let mut nat = [0i64; 64];
        let mut px = [0u8; 64];
        for bx in 0..bw {
            let blk = &comp.coef[(by * bw + bx) * 64..(by * bw + bx) * 64 + 64];
            for k in 0..64 { nat[ZZN[k]] = blk[k] as i64 * q[ZZN[k]]; }
            idct_islow(&nat, &mut px);
            for r in 0..8 { rows[r * stride + bx * 8..r * stride + bx * 8 + 8].copy_from_slice(&px[r * 8..r * 8 + 8]); }
        }
    });
    if tw == stride && th == bh * 8 { return plane; }
    let mut out = Vec::with_capacity(tw * th);
    for y in 0..th { out.extend_from_slice(&plane[y * stride..y * stride + tw]); }
    out
}

/// libjpeg's h2v2_fancy_upsample on a whole plane (jdsample.c); a plane no wider than 2 samples uses the box filter.
pub fn upsample_h2v2_fancy(p: &[u8], w: usize, h: usize) -> Vec<u8> {
    let ow = 2 * w;
    let mut out = vec![0u8; ow * 2 * h];
    if w <= 2 {
        for y in 0..h { for x in 0..w { let v = p[y * w + x]; for dy in 0..2 { for dx in 0..2 { out[(2 * y + dy) * ow + 2 * x + dx] = v; } } } }
        return out;
    }
    out.par_chunks_mut(ow * 2).enumerate().for_each(|(y, rows)| {
        let up = if y > 0 { y - 1 } else { 0 };
        let down = if y + 1 < h { y + 1 } else { h - 1 };
        for (v, other) in [(0usize, up), (1usize, down)] {
            let cur = &p[y * w..y * w + w];
            let oth = &p[other * w..other * w + w];
            let s = |x: usize| 3 * cur[x] as i32 + oth[x] as i32;
            let o = &mut rows[v * ow..v * ow + ow];
            o[0] = ((s(0) * 4 + 8) >> 4) as u8;
            o[1] = ((s(0) * 3 + s(1) + 7) >> 4) as u8;
            for x in 1..w - 1 {
                o[2 * x] = ((s(x) * 3 + s(x - 1) + 8) >> 4) as u8;
                o[2 * x + 1] = ((s(x) * 3 + s(x + 1) + 7) >> 4) as u8;
            }
            o[ow - 2] = ((s(w - 1) * 3 + s(w - 2) + 8) >> 4) as u8;
            o[ow - 1] = ((s(w - 1) * 4 + 7) >> 4) as u8;
        }
    });
    out
}

/// libjpeg's h2v1_fancy_upsample (4:2:2): triangle filter 3:1 along the row.
pub fn upsample_h2v1_fancy(p: &[u8], w: usize, h: usize) -> Vec<u8> {
    let ow = 2 * w;
    let mut out = vec![0u8; ow * h];
    out.par_chunks_mut(ow).enumerate().for_each(|(y, o)| {
        let r = &p[y * w..y * w + w];
        if w <= 2 { for x in 0..w { o[2 * x] = r[x]; o[2 * x + 1] = r[x]; } return; }
        o[0] = r[0];
        o[1] = ((r[0] as i32 * 3 + r[1] as i32 + 2) >> 2) as u8;
        for x in 1..w - 1 {
            let c = r[x] as i32 * 3;
            o[2 * x] = ((c + r[x - 1] as i32 + 1) >> 2) as u8;
            o[2 * x + 1] = ((c + r[x + 1] as i32 + 2) >> 2) as u8;
        }
        o[ow - 2] = ((r[w - 1] as i32 * 3 + r[w - 2] as i32 + 1) >> 2) as u8;
        o[ow - 1] = r[w - 1];
    });
    out
}

/// The TGY profile: the fancy filter on each 64 x 64 chroma tile on its own (the tile's edge samples replicated).
pub fn upsample_h2v2_tile(p: &[u8], w: usize, h: usize, tile: usize) -> Vec<u8> {
    let ow = 2 * w;
    let mut out = vec![0u8; ow * 2 * h];
    let mut y = 0;
    while y < h {
        let th = tile.min(h - y);
        let mut x = 0;
        while x < w {
            let tw = tile.min(w - x);
            let mut sub = Vec::with_capacity(tw * th);
            for r in 0..th { sub.extend_from_slice(&p[(y + r) * w + x..(y + r) * w + x + tw]); }
            let up = upsample_h2v2_fancy(&sub, tw, th);
            for r in 0..2 * th { out[(2 * y + r) * ow + 2 * x..(2 * y + r) * ow + 2 * x + 2 * tw].copy_from_slice(&up[r * 2 * tw..(r + 1) * 2 * tw]); }
            x += tile;
        }
        y += tile;
    }
    out
}

struct YccTabs { cr_r: [i32; 256], cb_b: [i32; 256], cr_g: [i32; 256], cb_g: [i32; 256] }
fn tabs() -> YccTabs {
    let fix = |x: f64| (x * 65536.0 + 0.5) as i64;
    let mut t = YccTabs { cr_r: [0; 256], cb_b: [0; 256], cr_g: [0; 256], cb_g: [0; 256] };
    for i in 0..256usize {
        let x = i as i64 - 128;
        t.cr_r[i] = ((fix(1.40200) * x + 32768) >> 16) as i32;
        t.cb_b[i] = ((fix(1.77200) * x + 32768) >> 16) as i32;
        t.cr_g[i] = (-fix(0.71414) * x) as i32;
        t.cb_g[i] = (-fix(0.34414) * x + 32768) as i32;
    }
    t
}

/// libjpeg's jdcolor.c integer conversion.
pub fn ycc_to_rgb(y: &[u8], cb: &[u8], cr: &[u8]) -> Vec<u8> {
    let t = tabs();
    let mut out = vec![0u8; y.len() * 3];
    out.par_chunks_mut(3 * 4096).enumerate().for_each(|(ci, chunk)| {
        let base = ci * 4096;
        for (i, px) in chunk.chunks_mut(3).enumerate() {
            let (yy, b, r) = (y[base + i] as i32, cb[base + i] as usize, cr[base + i] as usize);
            px[0] = (yy + t.cr_r[r]).clamp(0, 255) as u8;
            px[1] = (yy + ((t.cb_g[b] + t.cr_g[r]) >> 16)).clamp(0, 255) as u8;
            px[2] = (yy + t.cb_b[b]).clamp(0, 255) as u8;
        }
    });
    out
}

/// The picture's RGB (height x width x 3) from the coefficients, as libjpeg-turbo gives it; `tgy_profile` selects the tile-local chroma upsampling.
pub fn decode_pixels(j: &Jpeg, tgy_profile: bool) -> Result<Vec<u8>, String> {
    let hmax = j.components.iter().map(|c| c.h).max().unwrap();
    let vmax = j.components.iter().map(|c| c.v).max().unwrap();
    let (w, h) = (j.width, j.height);
    let mut planes: Vec<Vec<u8>> = Vec::new();
    for c in &j.components {
        let tw = (w * c.h).div_ceil(hmax);
        let th = (h * c.v).div_ceil(vmax);
        let q = j.quant[c.tq].as_ref().ok_or("missing quantisation table")?;
        let mut p = component_plane(c, q, tw, th);
        let (mut pw, mut ph) = (tw, th);
        if (c.h, c.v) != (hmax, vmax) {
            match (hmax / c.h, vmax / c.v, hmax % c.h, vmax % c.v) {
                (2, 2, 0, 0) => { p = if tgy_profile { upsample_h2v2_tile(&p, pw, ph, 64) } else { upsample_h2v2_fancy(&p, pw, ph) }; pw *= 2; ph *= 2; }
                (2, 1, 0, 0) => { p = upsample_h2v1_fancy(&p, pw, ph); pw *= 2; }
                _ => return Err("only 4:4:4, 4:2:2 and 4:2:0 are read".into()),
            }
        }
        // crop to the picture
        if pw != w || ph != h {
            let mut o = Vec::with_capacity(w * h);
            for y in 0..h { o.extend_from_slice(&p[y * pw..y * pw + w]); }
            p = o;
        }
        planes.push(p);
    }
    if planes.len() == 1 {
        let g = &planes[0];
        let mut out = vec![0u8; w * h * 3];
        out.par_chunks_mut(3).zip(g.par_iter()).for_each(|(px, &v)| { px[0] = v; px[1] = v; px[2] = v; });
        return Ok(out);
    }
    Ok(ycc_to_rgb(&planes[0], &planes[1], &planes[2]))
}

pub const TILE: usize = 128;

/// The picture's blocks as tiles of 128 x 128 luma samples: (ty, tx, grids) in raster order; a tile at the right or bottom edge keeps only the blocks that exist.
pub fn tile_grids(j: &Jpeg, ty: usize, tx: usize) -> Vec<Grid> {
    let hmax = j.components.iter().map(|c| c.h).max().unwrap();
    let vmax = j.components.iter().map(|c| c.v).max().unwrap();
    j.components.iter().map(|c| {
        let (pr, pc) = (TILE / 8 * c.v / vmax, TILE / 8 * c.h / hmax);
        let (r0, c0) = (ty * pr, tx * pc);
        let rows = pr.min(c.blocks_h - r0);
        let cols = pc.min(c.blocks_w - c0);
        let mut g = Grid::zeros(rows, cols);
        for y in 0..rows { for x in 0..cols {
            let o = ((r0 + y) * c.blocks_w + c0 + x) * 64;
            let b: &mut Block = &mut g.blocks[y * cols + x];
            b.copy_from_slice(&c.coef[o..o + 64]);
        } }
        g
    }).collect()
}

/// (tile rows, tile columns) of a picture.
pub fn tile_dims(j: &Jpeg) -> (usize, usize) {
    let hmax = j.components.iter().map(|c| c.h).max().unwrap();
    let vmax = j.components.iter().map(|c| c.v).max().unwrap();
    let c = &j.components[0];
    let (pr, pc) = (TILE / 8 * c.v / vmax, TILE / 8 * c.h / hmax);
    (c.blocks_h.div_ceil(pr), c.blocks_w.div_ceil(pc))
}
