//! One source picture, ready for every level: decoded to 8-bit RGB, converted to sRGB when it carries another ICC profile, the EXIF orientation applied;
//! plus its EXIF details. (Python: `tgy.prepare`, `tgy.exif_details`, and the decoding that Pillow does there.)
use crate::{jpegcoef, recon};
use rayon::prelude::*;
use std::io::Cursor;

pub struct Details {
    pub format: String,
    pub camera: Option<String>,
    pub lens: Option<String>,
    pub taken: Option<(u16, u8, u8, u8, u8, u8)>,
    pub shutter: Option<(u32, u32)>,
    pub aperture: Option<u16>,
    pub iso: Option<u32>,
    pub focal: Option<u16>,
}

pub struct Prepared {
    pub rgb: Vec<u8>,
    pub w: usize,
    pub h: usize,
    pub details: Details,
    /// The JPEG's own coefficients, when the picture is a JPEG whose zoom level may keep them (no ICC conversion, no orientation, 4:2:0, 4:4:4 or grey).
    pub lossless: Option<jpegcoef::Jpeg>,
}

pub fn sniff(d: &[u8]) -> Option<&'static str> {
    if d.len() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF { Some("JPEG") }
    else if d.starts_with(b"\x89PNG\r\n\x1a\n") { Some("PNG") }
    else if d.starts_with(b"GIF8") { Some("GIF") }
    else if d.starts_with(b"BM") { Some("BMP") }
    else if d.starts_with(b"II*\0") || d.starts_with(b"MM\0*") { Some("TIFF") }
    else if d.len() >= 12 && &d[0..4] == b"RIFF" && &d[8..12] == b"WEBP" { Some("WEBP") }
    else { None }
}

/// The ICC profile of a JPEG (APP2 "ICC_PROFILE" chunks, in sequence order).
fn jpeg_icc(d: &[u8]) -> Option<Vec<u8>> {
    let mut chunks: Vec<(u8, Vec<u8>)> = Vec::new();
    let mut p = 2;
    while p + 4 <= d.len() {
        if d[p] != 0xFF { p += 1; continue; }
        let m = d[p + 1];
        if m == 0xFF { p += 1; continue; }
        if m == 0xD8 || m == 0x01 || (0xD0..=0xD7).contains(&m) { p += 2; continue; }
        if m == 0xDA || m == 0xD9 { break; }
        let ln = ((d[p + 2] as usize) << 8) | d[p + 3] as usize;
        if ln < 2 || p + 2 + ln > d.len() { break; }
        let body = &d[p + 4..p + 2 + ln];
        if m == 0xE2 && body.len() > 14 && &body[..12] == b"ICC_PROFILE\0" { chunks.push((body[12], body[14..].to_vec())); }
        p += 2 + ln;
    }
    if chunks.is_empty() { return None; }
    chunks.sort_by_key(|c| c.0);
    Some(chunks.into_iter().flat_map(|c| c.1).collect())
}

/// The profile description (the 'desc' tag) as text, for the "is it sRGB" test.
fn icc_description(icc: &[u8]) -> Option<String> {
    if icc.len() < 132 { return None; }
    let be = |o: usize| u32::from_be_bytes([icc[o], icc[o + 1], icc[o + 2], icc[o + 3]]) as usize;
    let n = be(128);
    for i in 0..n.min(200) {
        let e = 132 + i * 12;
        if e + 12 > icc.len() { return None; }
        if &icc[e..e + 4] == b"desc" {
            let (off, size) = (be(e + 4), be(e + 8));
            let t = icc.get(off..off.checked_add(size)?)?;
            if t.len() < 12 { return None; }
            if &t[..4] == b"desc" {
                let cnt = u32::from_be_bytes([t[8], t[9], t[10], t[11]]) as usize;
                return Some(String::from_utf8_lossy(t.get(12..12 + cnt.saturating_sub(1))?).into_owned());
            } else if &t[..4] == b"mluc" && t.len() >= 28 {
                let (len, o) = (u32::from_be_bytes([t[20], t[21], t[22], t[23]]) as usize, u32::from_be_bytes([t[24], t[25], t[26], t[27]]) as usize);
                let s = t.get(o..o + len)?;
                let u: Vec<u16> = s.chunks(2).filter(|c| c.len() == 2).map(|c| u16::from_be_bytes([c[0], c[1]])).collect();
                return Some(String::from_utf16_lossy(&u));
            }
            return None;
        }
    }
    None
}

/// True when the profile is readable and is not sRGB (the case where the colours are converted).
pub fn profile_needs_srgb(icc: &Option<Vec<u8>>) -> bool {
    match icc {
        Some(p) => match icc_description(p) { Some(d) => !d.to_lowercase().contains("srgb") && moxcms::ColorProfile::new_from_slice(p).is_ok(), None => false },
        None => false,
    }
}

fn to_srgb(rgb: &mut Vec<u8>, icc: &[u8]) {
    use moxcms::{ColorProfile, Layout, RenderingIntent, TransformOptions};
    let Ok(src) = ColorProfile::new_from_slice(icc) else { return };
    let dst = ColorProfile::new_srgb();
    let opts = TransformOptions { rendering_intent: RenderingIntent::Perceptual, prefer_fixed_point: false, ..TransformOptions::default() };
    let Ok(t) = src.create_transform_8bit(Layout::Rgb, &dst, Layout::Rgb, opts) else { return };
    let mut out = vec![0u8; rgb.len()];
    let chunk = 3 * 65536;
    let ok = out.par_chunks_mut(chunk).zip(rgb.par_chunks(chunk)).all(|(o, i)| t.transform(i, o).is_ok());
    if ok { *rgb = out; }
}

fn orient_of(d: &[u8]) -> u8 {
    let Ok(ex) = exif::Reader::new().read_from_container(&mut Cursor::new(d)) else { return 1 };
    ex.get_field(exif::Tag::Orientation, exif::In::PRIMARY).and_then(|f| f.value.get_uint(0)).map(|v| v as u8).unwrap_or(1)
}

/// EXIF orientation applied to the pixels (the transforms of ImageOps.exif_transpose). Returns the new size.
pub fn transpose(rgb: Vec<u8>, w: usize, h: usize, o: u8) -> (Vec<u8>, usize, usize) {
    if !(2..=8).contains(&o) { return (rgb, w, h); }
    let (nw, nh) = if o >= 5 { (h, w) } else { (w, h) };
    let mut out = vec![0u8; nw * nh * 3];
    out.par_chunks_mut(nw * 3).enumerate().for_each(|(j, row)| {
        for i in 0..nw {
            let (sx, sy) = match o {
                2 => (w - 1 - i, j),
                3 => (w - 1 - i, h - 1 - j),
                4 => (i, h - 1 - j),
                5 => (j, i),
                6 => (j, h - 1 - i),
                7 => (w - 1 - j, h - 1 - i),
                _ => (w - 1 - j, i),
            };
            row[i * 3..i * 3 + 3].copy_from_slice(&rgb[(sy * w + sx) * 3..(sy * w + sx) * 3 + 3]);
        }
    });
    (out, nw, nh)
}

fn strip(s: &[u8]) -> String { String::from_utf8_lossy(s).trim_matches(|c| c == '\0' || c == ' ').to_string() }

fn limit_denominator(x: f64, max_den: u128) -> Option<(u128, u128)> {
    // the exact binary value of x as n / d, then CPython's Fraction.limit_denominator
    if !(x.is_finite() && x > 0.0) { return None; }
    let bits = x.to_bits();
    let exp = ((bits >> 52) & 0x7FF) as i32;
    let man = if exp == 0 { (bits & ((1 << 52) - 1)) << 1 } else { (bits & ((1 << 52) - 1)) | (1 << 52) } as u128;
    let e = if exp == 0 { -1074 } else { exp - 1075 };
    let (mut n, mut d) = if e >= 0 { (man.checked_shl(e as u32)?, 1u128) } else { if -e > 120 { return None; } (man, 1u128 << (-e)) };
    let g = { let (mut a, mut b) = (n, d); while b != 0 { let t = a % b; a = b; b = t; } a };
    n /= g; d /= g;
    if d <= max_den { return Some((n, d)); }
    let (sn, sd) = (n, d);
    let (mut p0, mut q0, mut p1, mut q1) = (0u128, 1u128, 1u128, 0u128);
    loop {
        let a = n / d;
        let q2 = q0 + a * q1;
        if q2 > max_den { break; }
        let np = p0 + a * p1;
        p0 = p1; q0 = q1; p1 = np; q1 = q2;
        let r = n - a * d; n = d; d = r;
    }
    let k = (max_den - q0) / q1;
    let (b1n, b1d) = (p0 + k * p1, q0 + k * q1);
    let dist = |pn: u128, pd: u128| -> f64 { ((pn as f64) / (pd as f64) - (sn as f64) / (sd as f64)).abs() };
    Some(if dist(p1, q1) <= dist(b1n, b1d) { (p1, q1) } else { (b1n, b1d) })
}

fn exif_details(d: &[u8], format: &str) -> Details {
    let mut out = Details { format: format.to_string(), camera: None, lens: None, taken: None, shutter: None, aperture: None, iso: None, focal: None };
    let Ok(ex) = exif::Reader::new().read_from_container(&mut Cursor::new(d)) else { return out };
    let get = |t: exif::Tag| ex.get_field(t, exif::In::PRIMARY);
    let ascii = |t: exif::Tag| -> String { match get(t).map(|f| &f.value) { Some(exif::Value::Ascii(v)) if !v.is_empty() => strip(&v[0]), _ => String::new() } };
    let rat = |t: exif::Tag| -> Option<f64> {
        match get(t).map(|f| &f.value) {
            Some(exif::Value::Rational(v)) if !v.is_empty() && v[0].denom != 0 => Some(v[0].num as f64 / v[0].denom as f64),
            Some(exif::Value::SRational(v)) if !v.is_empty() && v[0].denom != 0 => Some(v[0].num as f64 / v[0].denom as f64),
            Some(exif::Value::Double(v)) if !v.is_empty() => Some(v[0]),
            Some(exif::Value::Float(v)) if !v.is_empty() => Some(v[0] as f64),
            _ => None,
        }
    };
    let (mk, md) = (ascii(exif::Tag::Make), ascii(exif::Tag::Model));
    if !md.is_empty() { out.camera = Some(if mk.is_empty() || md.to_lowercase().starts_with(&mk.to_lowercase()) { md } else { format!("{mk} {md}") }); } else if !mk.is_empty() { out.camera = Some(mk); }
    let lens = ascii(exif::Tag::LensModel);
    if !lens.is_empty() { out.lens = Some(lens); }
    let mut ts = ascii(exif::Tag::DateTimeOriginal);
    if ts.is_empty() { ts = ascii(exif::Tag::DateTime); }
    let parts: Vec<&str> = ts.split(' ').collect();
    if parts.len() == 2 {
        let a: Vec<Option<u32>> = parts[0].split(':').map(|x| x.parse().ok()).collect();
        let b: Vec<Option<u32>> = parts[1].split(':').map(|x| x.parse().ok()).collect();
        if a.len() == 3 && b.len() == 3 && a.iter().chain(b.iter()).all(|x| x.is_some()) {
            let (y, mo, dd, h, mi, s) = (a[0].unwrap(), a[1].unwrap(), a[2].unwrap(), b[0].unwrap(), b[1].unwrap(), b[2].unwrap());
            if (1990..=2100).contains(&y) && (1..=12).contains(&mo) && (1..=31).contains(&dd) && h < 24 && mi < 60 && s < 61 { out.taken = Some((y as u16, mo as u8, dd as u8, h as u8, mi as u8, s.min(59) as u8)); }
        }
    }
    if let Some(et) = rat(exif::Tag::ExposureTime) { if et > 0.0 { if let Some((n, dn)) = limit_denominator(et, 100000) { if n > 0 && n <= u32::MAX as u128 { out.shutter = Some((n as u32, dn as u32)); } } } }
    if let Some(f) = rat(exif::Tag::FNumber) { if f > 0.0 && f < 6553.0 { out.aperture = Some((f * 10.0).round_ties_even() as u16); } }
    if let Some(v) = get(exif::Tag::PhotographicSensitivity).and_then(|f| f.value.get_uint(0)) { if v > 0 { out.iso = Some(v); } }
    if let Some(f) = rat(exif::Tag::FocalLength) { if f > 0.0 && f < 6553.0 { out.focal = Some((f * 10.0).round_ties_even() as u16); } }
    out
}

fn decode_other(d: &[u8], fmt: &'static str) -> Result<(Vec<u8>, usize, usize, Option<Vec<u8>>), String> {
    use image::{ImageDecoder, ImageFormat};
    let f = match fmt { "PNG" => ImageFormat::Png, "GIF" => ImageFormat::Gif, "BMP" => ImageFormat::Bmp, "TIFF" => ImageFormat::Tiff, _ => ImageFormat::WebP };
    let mut reader = image::ImageReader::with_format(Cursor::new(d), f);
    reader.no_limits();
    let mut dec: Box<dyn ImageDecoder> = match f {
        ImageFormat::Png => Box::new(image::codecs::png::PngDecoder::new(Cursor::new(d)).map_err(|e| e.to_string())?),
        ImageFormat::Tiff => Box::new(image::codecs::tiff::TiffDecoder::new(Cursor::new(d)).map_err(|e| e.to_string())?),
        ImageFormat::WebP => Box::new(image::codecs::webp::WebPDecoder::new(Cursor::new(d)).map_err(|e| e.to_string())?),
        _ => { let img = reader.decode().map_err(|e| e.to_string())?.to_rgb8(); let (w, h) = (img.width() as usize, img.height() as usize); return Ok((img.into_raw(), w, h, None)); }
    };
    dec.set_limits(image::Limits::no_limits()).map_err(|e| e.to_string())?;           // the library's default memory caps (512 MB) refuse a large scan; Pillow's bomb check was off in the Python converter too
    let icc = dec.icc_profile().ok().flatten();
    let img = image::DynamicImage::from_decoder(dec).map_err(|e| e.to_string())?.to_rgb8();
    let (w, h) = (img.width() as usize, img.height() as usize);
    Ok((img.into_raw(), w, h, icc))
}

/// Decodes and prepares a picture file's bytes.
pub fn prepare(d: &[u8]) -> Result<Prepared, String> {
    let fmt = sniff(d).ok_or("cannot identify image file")?;
    let orient = orient_of(d);
    let details = exif_details(d, fmt);
    let (mut rgb, mut w, mut h, icc, mut jpeg) = if fmt == "JPEG" {
        let j = jpegcoef::read_jpeg(d)?;
        let rgb = recon::decode_pixels(&j, false)?;
        let icc = if j.components.len() == 3 { jpeg_icc(d) } else { None };
        let (w, h) = (j.width, j.height);
        (rgb, w, h, icc, Some(j))
    } else {
        let (rgb, w, h, icc) = decode_other(d, fmt)?;
        (rgb, w, h, icc, None)
    };
    let srgb = profile_needs_srgb(&icc);
    if srgb { if let Some(p) = &icc { to_srgb(&mut rgb, p); } }
    if (2..=8).contains(&orient) { let r = transpose(rgb, w, h, orient); rgb = r.0; w = r.1; h = r.2; }
    // the JPEG's own coefficients stay usable only when nothing changed the picture: no colour conversion, no orientation, and a layout the core's decoder runs
    if srgb || (2..=8).contains(&orient) { jpeg = None; }
    if let Some(j) = &jpeg {
        let ok = match j.components.len() {
            1 => true,
            3 => { let c = &j.components; (c[1].h, c[1].v, c[2].h, c[2].v) == (1, 1, 1, 1) && ((c[0].h, c[0].v) == (1, 1) || (c[0].h, c[0].v) == (2, 2)) && j.quant[c[1].tq].is_some() && c[1].tq == c[2].tq }
            _ => false,
        };
        if !ok { jpeg = None; }
    }
    Ok(Prepared { rgb, w, h, details, lossless: jpeg })
}
