//! TGY encoder for pixel sources (a port of `tools/gallery/tgy_encode.py`): RGB pixels to quantised DCT coefficients, 4:2:0, the JPEG standard
//! tables scaled by the libjpeg formula, edge replication into partial blocks, round half away from zero. Works one MCU row at a time, so a
//! very large picture never needs a floating-point copy of itself.
use crate::jpegcoef::{Comp, Jpeg};
use crate::v0::ZZN;
use rayon::prelude::*;

pub const STD_LUMA: [u32; 64] = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99];
const fn std_chroma() -> [u32; 64] {
    let mut t = [99u32; 64];
    let head = [17u32, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99];
    let mut i = 0;
    while i < 32 { t[i] = head[i]; i += 1; }
    t
}
pub const STD_CHROMA: [u32; 64] = std_chroma();

/// libjpeg's quality scaling of a base table (natural order), returned in ZIGZAG order.
pub fn scaled_table(base: &[u32; 64], quality: u32) -> [u16; 64] {
    let q = quality.clamp(1, 100);
    let s = if q < 50 { 5000 / q } else { 200 - 2 * q };
    let mut nat = [0u32; 64];
    for i in 0..64 { nat[i] = ((base[i] * s + 50) / 100).clamp(1, 255); }
    let mut z = [0u16; 64];
    for k in 0..64 { z[k] = nat[ZZN[k]] as u16; }
    z
}

fn cmatrix() -> [[f64; 8]; 8] {
    let mut c = [[0f64; 8]; 8];
    for u in 0..8 { for x in 0..8 {
        let a = if u == 0 { 1.0 } else { 2.0 };
        c[u][x] = (a / 8.0f64).sqrt() * (((2 * x + 1) as f64) * (u as f64) * std::f64::consts::PI / 16.0).cos();
    } }
    c
}

/// F = C X C^T for one block of level-shifted samples (row-major), divided by the quantiser (natural order), rounded half away from zero, to zigzag order.
#[inline]
fn fdct_quant(c: &[[f64; 8]; 8], x: &[f64; 64], qnat: &[f64; 64], out: &mut [i16]) {
    let mut nat = [0i16; 64];
    for u in 0..8 { for v in 0..8 {
        let mut s = 0.0f64;
        // the rows of X against the column of C first, then the column of C against that (a fixed order: the sums are the same to the last bit on every machine)
        for xx in 0..8 {
            let mut t = 0.0f64;
            for y in 0..8 { t += x[xx * 8 + y] * c[v][y]; }
            s += c[u][xx] * t;
        }
        let f = s / qnat[u * 8 + v];
        let r = f.signum() * (f.abs() + 0.5).floor();
        nat[u * 8 + v] = if f == 0.0 { 0 } else { r as i16 };
    } }
    for k in 0..64 { out[k] = nat[ZZN[k]]; }
}

/// RGB (w x h x 3) to a 4:2:0 coefficient picture at `quality`.
pub fn encode_rgb(rgb: &[u8], w: usize, h: usize, quality: u32) -> Jpeg {
    assert_eq!(rgb.len(), w * h * 3);
    let qy = scaled_table(&STD_LUMA, quality);
    let qc = scaled_table(&STD_CHROMA, quality);
    let (mw, mh) = (w.div_ceil(16), h.div_ceil(16));
    let (cw, ch) = (w.div_ceil(2), h.div_ceil(2)); // the chroma plane's real size
    let mut comps = vec![
        Comp { id: 1, h: 2, v: 2, tq: 0, blocks_w: mw * 2, blocks_h: mh * 2, true_w: 0, true_h: 0, coef: vec![0; mw * 2 * mh * 2 * 64] },
        Comp { id: 2, h: 1, v: 1, tq: 1, blocks_w: mw, blocks_h: mh, true_w: 0, true_h: 0, coef: vec![0; mw * mh * 64] },
        Comp { id: 3, h: 1, v: 1, tq: 1, blocks_w: mw, blocks_h: mh, true_w: 0, true_h: 0, coef: vec![0; mw * mh * 64] },
    ];
    let cm = cmatrix();
    let mut qn_y = [0f64; 64];
    let mut qn_c = [0f64; 64];
    for k in 0..64 { qn_y[ZZN[k]] = qy[k] as f64; qn_c[ZZN[k]] = qc[k] as f64; }
    let (c0, rest) = comps.split_at_mut(1);
    let (c1, c2) = rest.split_at_mut(1);
    let (yc, cbc, crc) = (&mut c0[0], &mut c1[0], &mut c2[0]);
    let ybw = yc.blocks_w;
    // one MCU row (16 picture rows) per task
    yc.coef.par_chunks_mut(ybw * 2 * 64).zip(cbc.coef.par_chunks_mut(mw * 64)).zip(crc.coef.par_chunks_mut(mw * 64)).enumerate().for_each(|(my, ((ycoef, cbcoef), crcoef))| {
        let px = |yy: usize, xx: usize| { let o = (yy.min(h - 1) * w + xx.min(w - 1)) * 3; (rgb[o] as f64, rgb[o + 1] as f64, rgb[o + 2] as f64) };
        let ycc = |yy: usize, xx: usize| { let (r, g, b) = px(yy, xx); (0.299 * r + 0.587 * g + 0.114 * b, -0.168736 * r - 0.331264 * g + 0.5 * b + 128.0, 0.5 * r - 0.418688 * g - 0.081312 * b + 128.0) };
        // luma: 2 x (2 * mw) blocks of this MCU row; the plane is edge-replicated
        let mut blk = [0f64; 64];
        for by in 0..2 { for bx in 0..ybw {
            for r in 0..8 { for c in 0..8 { blk[r * 8 + c] = ycc(my * 16 + by * 8 + r, bx * 8 + c).0 - 128.0; } }
            let o = (by * ybw + bx) * 64;
            fdct_quant(&cm, &blk, &qn_y, &mut ycoef[o..o + 64]);
        } }
        // chroma: the 2 x 2 box average of the edge-replicated-to-even plane, then edge-replicated to whole blocks
        for bx in 0..mw {
            let mut bcb = [0f64; 64];
            let mut bcr = [0f64; 64];
            for r in 0..8 { for c in 0..8 {
                let cy = (my * 8 + r).min(ch - 1);
                let cx = (bx * 8 + c).min(cw - 1);
                let (a, b, cc, d) = (ycc(2 * cy, 2 * cx), ycc(2 * cy + 1, 2 * cx), ycc(2 * cy, 2 * cx + 1), ycc(2 * cy + 1, 2 * cx + 1));
                bcb[r * 8 + c] = (((a.1 + b.1) + cc.1) + d.1) / 4.0 - 128.0;
                bcr[r * 8 + c] = (((a.2 + b.2) + cc.2) + d.2) / 4.0 - 128.0;
            } }
            fdct_quant(&cm, &bcb, &qn_c, &mut cbcoef[bx * 64..bx * 64 + 64]);
            fdct_quant(&cm, &bcr, &qn_c, &mut crcoef[bx * 64..bx * 64 + 64]);
        }
    });
    let mut quant = [None, None, None, None];
    quant[0] = Some(qy);
    quant[1] = Some(qc);
    Jpeg { width: w, height: h, progressive: false, restart_interval: 0, quant, components: comps, mcus_x: mw, mcus_y: mh }
}
