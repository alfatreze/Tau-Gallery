//! Pillow's Image.resize(..., LANCZOS) for 8-bit RGB, reproduced operation for operation (Resample.c: the filter coefficients in double, normalised,
//! turned into 22-bit fixed point, a horizontal pass and then a vertical pass, each pixel rounded with half and clipped), so the fit view, the
//! thumbnails and the zoom levels come out the same as the Python converter's.
use rayon::prelude::*;

const PRECISION_BITS: u32 = 32 - 8 - 2;
const SUPPORT: f64 = 3.0;

fn sinc(x: f64) -> f64 { if x == 0.0 { 1.0 } else { let x = x * std::f64::consts::PI; x.sin() / x } }
fn lanczos(x: f64) -> f64 { if (-3.0..3.0).contains(&x) { sinc(x) * sinc(x / 3.0) } else { 0.0 } }

struct Coeffs { bounds: Vec<(usize, usize)>, ksize: usize, kk: Vec<i32> }

fn precompute(in_size: usize, out_size: usize) -> Coeffs {
    let scale = in_size as f64 / out_size as f64;
    let filterscale = scale.max(1.0);
    let support = SUPPORT * filterscale;
    let ksize = support.ceil() as usize * 2 + 1;
    let mut kk = vec![0i32; out_size * ksize];
    let mut bounds = Vec::with_capacity(out_size);
    let mut pre = vec![0f64; ksize];
    for xx in 0..out_size {
        let center = (xx as f64 + 0.5) * scale;
        let ss = 1.0 / filterscale;
        let mut xmin = (center - support + 0.5) as i64;
        if xmin < 0 { xmin = 0; }
        let mut xmax = (center + support + 0.5) as i64;
        if xmax > in_size as i64 { xmax = in_size as i64; }
        let xmax = (xmax - xmin) as usize;
        let mut ww = 0.0;
        for x in 0..xmax {
            let w = lanczos((x as f64 + xmin as f64 - center + 0.5) * ss);
            pre[x] = w;
            ww += w;
        }
        for x in 0..ksize {
            let w = if x < xmax { if ww != 0.0 { pre[x] / ww } else { pre[x] } } else { 0.0 };
            kk[xx * ksize + x] = if w < 0.0 { (-0.5 + w * (1u64 << PRECISION_BITS) as f64) as i32 } else { (0.5 + w * (1u64 << PRECISION_BITS) as f64) as i32 };
        }
        bounds.push((xmin as usize, xmax));
    }
    Coeffs { bounds, ksize, kk }
}

#[inline]
fn clip8(ss: i32) -> u8 { (ss >> PRECISION_BITS).clamp(0, 255) as u8 }

/// `src` is sw x sh RGB; the result is dw x dh RGB.
pub fn resize_rgb(src: &[u8], sw: usize, sh: usize, dw: usize, dh: usize) -> Vec<u8> {
    assert_eq!(src.len(), sw * sh * 3);
    let need_h = dw != sw;
    let need_v = dh != sh;
    if !need_h && !need_v { return src.to_vec(); }
    let vc = precompute(sh, dh);
    let (first, last) = if need_v { (vc.bounds[0].0, vc.bounds[dh - 1].0 + vc.bounds[dh - 1].1) } else { (0, sh) };
    let rows = last - first;
    // horizontal pass over the rows the vertical pass needs
    let temp: Vec<u8> = if need_h {
        let hc = precompute(sw, dw);
        let mut t = vec![0u8; rows * dw * 3];
        t.par_chunks_mut(dw * 3).enumerate().for_each(|(r, o)| {
            let row = &src[(first + r) * sw * 3..(first + r + 1) * sw * 3];
            for xx in 0..dw {
                let (xmin, xmax) = hc.bounds[xx];
                let k = &hc.kk[xx * hc.ksize..xx * hc.ksize + xmax];
                let (mut s0, mut s1, mut s2) = (1i32 << (PRECISION_BITS - 1), 1i32 << (PRECISION_BITS - 1), 1i32 << (PRECISION_BITS - 1));
                for (x, &kv) in k.iter().enumerate() {
                    let p = &row[(xmin + x) * 3..(xmin + x) * 3 + 3];
                    s0 += p[0] as i32 * kv;
                    s1 += p[1] as i32 * kv;
                    s2 += p[2] as i32 * kv;
                }
                o[xx * 3] = clip8(s0);
                o[xx * 3 + 1] = clip8(s1);
                o[xx * 3 + 2] = clip8(s2);
            }
        });
        t
    } else if first == 0 && last == sh { return resize_v_only(src, sw, dh, &vc, 0); } else { src[first * sw * 3..last * sw * 3].to_vec() };
    if !need_v { return temp; }
    resize_v_only(&temp, dw, dh, &vc, first)
}

fn resize_v_only(t: &[u8], w: usize, dh: usize, vc: &Coeffs, first: usize) -> Vec<u8> {
    let mut out = vec![0u8; w * dh * 3];
    let stride = w * 3;
    out.par_chunks_mut(stride).enumerate().for_each(|(yy, o)| {
        let (ymin, ymax) = vc.bounds[yy];
        let k = &vc.kk[yy * vc.ksize..yy * vc.ksize + ymax];
        for x in 0..stride {
            let mut s = 1i32 << (PRECISION_BITS - 1);
            for (y, &kv) in k.iter().enumerate() { s += t[(ymin + y - first) * stride + x] as i32 * kv; }
            o[x] = clip8(s);
        }
    });
    out
}
