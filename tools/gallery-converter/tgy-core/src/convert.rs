//! The converter: a folder or a picture into Tau Gallery files (a port of `tools/gallery/tgy_convert.py` and `tgy_make.canvas_container`, `tgy.py`).
//! One `.tgy` per picture and the library index; the output is the card's own tree. Events are the same one-JSON-object-a-line records the Python tool printed.
use crate::{container, encode, index, picture, resize};
use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

pub const CANVAS_W: usize = 800;
pub const CANVAS_H: usize = 720;
const THUMB_W: usize = 96;
const THUMB_H: usize = 72;
const Q16: i64 = 65536;
const TILE: usize = 128;
const RASTER_MAX_BYTES: usize = 9437184;
const EXT: [&str; 8] = ["jpg", "jpeg", "png", "bmp", "gif", "tif", "tiff", "webp"];

#[derive(Debug, Clone)]
pub struct Options {
    pub src: PathBuf,
    pub out: PathBuf,
    pub card_dir: String,
    pub index_name: String,
    pub upscale: bool,
    pub levels: bool,
    pub format_v1: bool,
    pub quality: u32,
    pub fit_quality: u32,
    pub card_layout: bool,
    pub merge: bool,
    pub index_v1: bool,
    pub sort: String,
    pub nested: String,          // folders inside a folder: "flatten" (default: into the first level folder they are under) or "join" (a folder of their own named with the whole path, "Trip - Day 1")
}

impl Options {
    pub fn new(src: PathBuf, out: PathBuf) -> Options {
        Options { src, out, card_dir: "Assets/tau_gallery/common/photos".into(), index_name: "tau-library.tdb".into(), upscale: false, levels: false, format_v1: false, quality: 92, fit_quality: 90, card_layout: false, merge: false, index_v1: true, sort: "name".into(), nested: "flatten".into() }
    }
}

/// The picture's rectangle inside a canvas: scaled to fit (never enlarged unless `upscale`), centred; even x. (Python's round: half to even.)
pub fn fit_rect(w: usize, h: usize, cw: usize, ch: usize, upscale: bool) -> (usize, usize, usize, usize) {
    let mut s = (cw as f64 / w as f64).min(ch as f64 / h as f64);
    if !upscale { s = s.min(1.0); }
    let rw = ((w as f64 * s).round_ties_even() as usize).max(1).min(cw);
    let rh = ((h as f64 * s).round_ties_even() as usize).max(1).min(ch);
    ((cw - rw) / 2 / 2 * 2, (ch - rh) / 2, rw, rh)
}

pub fn rgb565_le(rgb: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(rgb.len() / 3 * 2);
    for p in rgb.chunks_exact(3) {
        let v = (((p[0] & 0xF8) as u16) << 8) | (((p[1] & 0xFC) as u16) << 3) | (p[2] >> 3) as u16;
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

fn paste(dst: &mut [u8], dw: usize, src: &[u8], sw: usize, sh: usize, x: usize, y: usize) {
    for r in 0..sh { dst[((y + r) * dw + x) * 3..((y + r) * dw + x + sw) * 3].copy_from_slice(&src[r * sw * 3..(r + 1) * sw * 3]); }
}

/// (canvas RGB888 800 x 720, content rect, thumbnail RGB565 96 x 72).
pub fn render(p: &picture::Prepared, upscale: bool) -> (Vec<u8>, (usize, usize, usize, usize), Vec<u8>) {
    let (x, y, rw, rh) = fit_rect(p.w, p.h, CANVAS_W, CANVAS_H, upscale);
    let (small, thumb) = rayon::join(
        || if (rw, rh) != (p.w, p.h) { resize::resize_rgb(&p.rgb, p.w, p.h, rw, rh) } else { p.rgb.clone() },
        || { let (tx, ty, tw, th) = fit_rect(p.w, p.h, THUMB_W, THUMB_H, false); let ts = resize::resize_rgb(&p.rgb, p.w, p.h, tw, th); let mut tc = vec![0u8; THUMB_W * THUMB_H * 3]; paste(&mut tc, THUMB_W, &ts, tw, th, (THUMB_W - tw) / 2, (THUMB_H - th) / 2); let _ = (tx, ty); rgb565_le(&tc) });
    let mut canvas = vec![0u8; CANVAS_W * CANVAS_H * 3];
    paste(&mut canvas, CANVAS_W, &small, rw, rh, x, y);
    (canvas, (x, y, rw, rh), thumb)
}

/// The picture scaled to cover a 96 x 96 square and cropped to it, RGB565.
fn square_thumb(p: &picture::Prepared) -> Vec<u8> {
    let s = (96.0 / p.w as f64).max(96.0 / p.h as f64);
    let (w, h) = (((p.w as f64 * s).round_ties_even() as usize).max(96), ((p.h as f64 * s).round_ties_even() as usize).max(96));
    let small = if (w, h) != (p.w, p.h) { resize::resize_rgb(&p.rgb, p.w, p.h, w, h) } else { p.rgb.clone() };
    let (x, y) = ((w - 96) / 2, (h - 96) / 2);
    let mut out = Vec::with_capacity(96 * 96 * 3);
    for r in 0..96 { out.extend_from_slice(&small[((y + r) * w + x) * 3..((y + r) * w + x + 96) * 3]); }
    rgb565_le(&out)
}

/// The version 1 container of a prepared picture (the hardware decoder's): the fit level is the whole 800 x 720 canvas, and a picture larger than the canvas also gets its full level.
pub fn container_v1(p: &picture::Prepared, canvas: &[u8], quality: u32, fit_quality: u32, thumb: &[u8], cancel: &AtomicBool) -> Result<Vec<u8>, String> {
    let fit = encode::encode_rgb(canvas, CANVAS_W, CANVAS_H, fit_quality);
    let owned;
    let mut levels = vec![container::Level { kind: container::KIND_FIT, pic: &fit, rect: (0, 0, CANVAS_W as u16, CANVAS_H as u16) }];
    if p.w > CANVAS_W || p.h > CANVAS_H {
        let full = match &p.lossless { Some(j) => j, None => { owned = encode::encode_rgb(&p.rgb, p.w, p.h, quality); &owned } };
        levels.push(container::Level { kind: container::KIND_FULL, pic: full, rect: (0, 0, p.w.min(65535) as u16, p.h.min(65535) as u16) });
    }
    container::write_container(p.w as u32, p.h as u32, 1, thumb, &levels, cancel)
}

// ---- the version 0 files: raw canvas, optional zoom levels (docs/M2_DESIGN.md)
fn ladder(sw: usize, sh: usize) -> Vec<(usize, usize)> {
    let (sw, sh) = (sw as i64, sh as i64);
    let fq = (CANVAS_W as i64 * Q16 / sw).min(CANVAS_H as i64 * Q16 / sh).min(Q16);
    let stop = |sc: i64| (((sw * sc + Q16 / 2) / Q16).max(1) as usize, ((sh * sc + Q16 / 2) / Q16).max(1) as usize);
    let mut out = vec![stop(fq)];
    if sw > CANVAS_W as i64 || sh > CANVAS_H as i64 {
        if (sw >= 1600 || sh >= 1440) && (Q16 / 2) * 2 >= fq * 3 { out.push(stop(Q16 / 2)); }
        out.push(stop(Q16));
    }
    out
}

enum ZoomLevel { Raster { w: usize, h: usize, rect: (usize, usize, usize, usize), stride: usize, data: Vec<u8> }, Tiles { w: usize, h: usize, data: Vec<u8> } }

fn zoom_levels(p: &picture::Prepared) -> Vec<ZoomLevel> {
    let mut out = Vec::new();
    for &(lw, lh) in &ladder(p.w, p.h)[1..] {
        let lv = if (lw, lh) == (p.w, p.h) { p.rgb.clone() } else { resize::resize_rgb(&p.rgb, p.w, p.h, lw, lh) };
        if lw.max(CANVAS_W) * lh.max(CANVAS_H) * 3 <= RASTER_MAX_BYTES {
            let (sw, sh) = (lw.max(CANVAS_W), lh.max(CANVAS_H));
            let (px, py) = (((sw - lw) / 2) & !1, (sh - lh) / 2);
            let mut canvas = vec![0u8; sw * sh * 3];
            paste(&mut canvas, sw, &lv, lw, lh, px, py);
            let stride = (sw * 3 + 3) / 4 * 4;
            let mut data = vec![0u8; stride * sh];
            for r in 0..sh { data[r * stride..r * stride + sw * 3].copy_from_slice(&canvas[r * sw * 3..(r + 1) * sw * 3]); }
            out.push(ZoomLevel::Raster { w: sw, h: sh, rect: (px, py, lw, lh), stride, data });
            continue;
        }
        if lw < CANVAS_W || lh < CANVAS_H { continue; }
        let (ntx, nty) = (lw.div_ceil(TILE), lh.div_ceil(TILE));
        let mut data = vec![0u8; ntx * nty * TILE * TILE * 3];
        for ty in 0..nty { for tx in 0..ntx {
            let o = (ty * ntx + tx) * TILE * TILE * 3;
            let cw = TILE.min(lw - tx * TILE);
            for r in 0..TILE.min(lh - ty * TILE) { data[o + r * TILE * 3..o + r * TILE * 3 + cw * 3].copy_from_slice(&lv[((ty * TILE + r) * lw + tx * TILE) * 3..((ty * TILE + r) * lw + tx * TILE + cw) * 3]); }
        } }
        out.push(ZoomLevel::Tiles { w: lw, h: lh, data });
    }
    out
}

fn pack_tgy_v0(canvas: &[u8], rect: (usize, usize, usize, usize), thumb: &[u8], size: (usize, usize), levels: &[ZoomLevel]) -> Vec<u8> {
    let al = |n: usize| n.div_ceil(4096) * 4096;
    let crc = |b: &[u8]| crc32fast::hash(b);
    let nlev = 1 + levels.len();
    let thumb_off = 4096;
    let data_off = thumb_off + al(index::THUMB_BYTES);
    let mut end = data_off + al(canvas.len());
    let mut entries: Vec<[u8; 48]> = Vec::new();
    let entry = |w: u32, h: u32, r: [u16; 4], b4: [u8; 4], stride: u32, off: u32, len: u32, c: u32| -> [u8; 48] {
        let mut e = [0u8; 48];
        e[0..4].copy_from_slice(&w.to_le_bytes()); e[4..8].copy_from_slice(&h.to_le_bytes());
        for k in 0..4 { e[8 + 2 * k..10 + 2 * k].copy_from_slice(&r[k].to_le_bytes()); }
        e[16..20].copy_from_slice(&b4);
        e[20..24].copy_from_slice(&stride.to_le_bytes()); e[24..28].copy_from_slice(&off.to_le_bytes()); e[28..32].copy_from_slice(&len.to_le_bytes());
        e[40..44].copy_from_slice(&c.to_le_bytes());
        e
    };
    entries.push(entry(CANVAS_W as u32, CANVAS_H as u32, [rect.0 as u16, rect.1 as u16, rect.2 as u16, rect.3 as u16], [0, 0, 0, 0], (CANVAS_W * 3) as u32, data_off as u32, canvas.len() as u32, crc(canvas)));
    let mut placed: Vec<(usize, &[u8])> = Vec::new();
    for l in levels {
        match l {
            ZoomLevel::Raster { w, h, rect, stride, data } => { entries.push(entry(*w as u32, *h as u32, [rect.0 as u16, rect.1 as u16, rect.2 as u16, rect.3 as u16], [0, 0, 0, 1], *stride as u32, end as u32, data.len() as u32, crc(data))); placed.push((end, data)); end += al(data.len()); }
            ZoomLevel::Tiles { w, h, data } => { entries.push(entry(*w as u32, *h as u32, [0, 0, *w as u16, *h as u16], [1, 0, 0, 1], (TILE * 3) as u32, end as u32, data.len() as u32, crc(data))); placed.push((end, data)); end += al(data.len()); }
        }
    }
    let file_size = end;
    let mut head = Vec::new();
    head.extend_from_slice(b"TGY\0"); head.extend_from_slice(&0u16.to_le_bytes()); head.extend_from_slice(&64u16.to_le_bytes());
    head.extend_from_slice(&(size.0 as u32).to_le_bytes()); head.extend_from_slice(&(size.1 as u32).to_le_bytes());
    head.extend_from_slice(&[3, 7, 0, 0]);
    for v in [nlev as u32, 64, thumb_off as u32, index::THUMB_BYTES as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    head.extend_from_slice(&96u16.to_le_bytes()); head.extend_from_slice(&72u16.to_le_bytes());
    for v in [0u32, 0, file_size as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    let c = crc(&head); head.extend_from_slice(&c.to_le_bytes()); head.resize(64, 0);
    let mut buf = vec![0u8; file_size];
    buf[..64].copy_from_slice(&head);
    for (i, e) in entries.iter().enumerate() { buf[64 + i * 48..64 + (i + 1) * 48].copy_from_slice(e); }
    buf[thumb_off..thumb_off + thumb.len()].copy_from_slice(thumb);
    buf[data_off..data_off + canvas.len()].copy_from_slice(canvas);
    for (off, d) in placed { buf[off..off + d.len()].copy_from_slice(d); }
    buf
}

// ---- file names
fn is_picture(p: &Path) -> bool {
    let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    !name.starts_with('.') && p.is_file() && p.extension().and_then(|e| e.to_str()).map(|e| EXT.contains(&e.to_ascii_lowercase().as_str())).unwrap_or(false)
}

fn ascii_stem(stem: &str) -> String {
    let s: String = stem.chars().map(|c| if (0x20..0x7F).contains(&(c as u32)) && !"<>:\"/\\|?*".contains(c) { c } else { '_' }).collect();
    if s != stem { format!("{s}_{:04x}", crc32fast::hash(stem.as_bytes()) & 0xFFFF) } else { s }
}

fn name_of(stem: &str) -> String { stem.chars().map(|c| if (c as u32) < 256 { c } else { '?' }).take(index::NAME_BYTES - 1).collect() }

fn write_atomic(path: &Path, data: &[u8]) -> std::io::Result<()> {
    let tmp = path.with_file_name(format!("{}.part", path.file_name().unwrap().to_string_lossy()));
    std::fs::write(&tmp, data)?;
    std::fs::rename(&tmp, path)
}

/// Seconds since 2000-01-01 00:00:00 of a clock reading (no time zone); 0 when unknown or invalid.
pub fn taken_seconds(t: Option<(u16, u8, u8, u8, u8, u8)>) -> u32 {
    let Some((y, mo, d, h, mi, s)) = t else { return 0 };
    let dim = [31, if (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 { 29 } else { 28 }, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    if !(1..=12).contains(&mo) || d < 1 || d > dim[mo as usize - 1] { return 0; }
    let days_from_civil = |y: i64, m: i64, d: i64| { let y = if m <= 2 { y - 1 } else { y }; let era = y.div_euclid(400); let yoe = y - era * 400; let doy = (153 * (if m > 2 { m - 3 } else { m + 9 }) + 2) / 5 + d - 1; let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; era * 146097 + doe - 719468 };
    let secs = (days_from_civil(y as i64, mo as i64, d as i64) - days_from_civil(2000, 1, 1)) * 86400 + h as i64 * 3600 + mi as i64 * 60 + s as i64;
    if secs > 0 { secs as u32 } else { 0 }
}

/// The library's folders under `src`: (name, pictures) in name order. A folder with pictures is a folder of the library; one inside another is flattened into the first level folder it is under (`mode` "flatten") or gets its own, named with the whole
/// path joined by " - " (`mode` "join"). Hidden folders (a name starting with a dot) are skipped at every depth. `list` gives the pictures directly in a folder, sorted (the same rule as `tools/gallery/tgy_convert.py nested_folders`).
fn nested_folders(src: &Path, mode: &str, list: &dyn Fn(&Path) -> Vec<PathBuf>) -> Vec<(String, Vec<PathBuf>)> {
    fn subdirs(d: &Path) -> Vec<PathBuf> {
        let mut v: Vec<PathBuf> = std::fs::read_dir(d).map(|rd| rd.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.is_dir() && !p.file_name().unwrap().to_string_lossy().starts_with('.')).collect()).unwrap_or_default();
        v.sort_by(|x, y| x.file_name().cmp(&y.file_name())); v
    }
    fn walk(d: &Path, names: &mut Vec<String>, list: &dyn Fn(&Path) -> Vec<PathBuf>, mode: &str, out: &mut Vec<(String, Vec<PathBuf>)>) {
        let fl = list(d);
        if !fl.is_empty() {
            let name = if mode == "flatten" { names[0].clone() } else { names.join(" - ") };
            match out.iter_mut().find(|(n, _)| *n == name) { Some((_, v)) => v.extend(fl), None => out.push((name, fl)) }
        }
        for q in subdirs(d) { names.push(q.file_name().unwrap().to_string_lossy().into_owned()); walk(&q, names, list, mode, out); names.pop(); }
    }
    let mut out: Vec<(String, Vec<PathBuf>)> = Vec::new();
    for q in subdirs(src) { let mut names = vec![q.file_name().unwrap().to_string_lossy().into_owned()]; walk(&q, &mut names, list, mode, &mut out); }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}
struct Item { path: String, name: String, size: u32, width: u32, height: u32, folder_name: String, taken: u32, details: Vec<u8>, thumb: Vec<u8>, order: usize }

/// Runs a conversion. `emit` receives each event; the result is the exit code (0 done, 2 a usage error). `cancel` stops it between pictures and between tiles.
pub fn run(a: &Options, cancel: &AtomicBool, emit: &mut dyn FnMut(Value)) -> i32 {
    let err = |emit: &mut dyn FnMut(Value), m: String| { emit(json!({"event": "error", "error": m})); 2 };
    let jobs: Vec<(PathBuf, Option<String>)>;
    if a.src.is_file() {
        let ok = a.src.extension().and_then(|e| e.to_str()).map(|e| EXT.contains(&e.to_ascii_lowercase().as_str())).unwrap_or(false);
        if !ok { return err(emit, format!("not a picture this tool reads: {}", a.src.file_name().unwrap_or_default().to_string_lossy())); }
        jobs = vec![(a.src.clone(), None)];
    } else if a.src.is_dir() {
        let list = |d: &Path| -> Vec<PathBuf> { let mut v: Vec<PathBuf> = std::fs::read_dir(d).map(|rd| rd.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| is_picture(p)).collect()).unwrap_or_default(); v.sort_by(|x, y| x.file_name().cmp(&y.file_name())); v };
        let top = list(&a.src);
        let subs = nested_folders(&a.src, &a.nested, &list);
        if a.index_v1 && !subs.is_empty() {
            let top_name = a.src.file_name().map(|n| n.to_string_lossy().into_owned()).filter(|s| !s.is_empty()).unwrap_or_else(|| index::ALL_PICTURES.to_string());
            let mut j: Vec<(PathBuf, Option<String>)> = Vec::new();
            for (n, fl) in &subs { for p in fl { j.push((p.clone(), Some(n.clone()))); } }
            for p in top { j.push((p, Some(top_name.clone()))); }
            jobs = j;
        } else { jobs = top.into_iter().map(|p| (p, None)).collect(); }
    } else { return err(emit, format!("no such file or folder: {}", a.src.display())); }
    let card_dir = a.card_dir.trim_matches('/').to_string();
    let photos = if a.card_layout { a.out.join(&card_dir) } else { a.out.clone() };
    let index_dir = if a.card_layout { a.out.join(Path::new(&card_dir).parent().unwrap_or(Path::new(""))) } else { a.out.clone() };
    if let Err(e) = std::fs::create_dir_all(&photos).and_then(|_| std::fs::create_dir_all(&index_dir)) { return err(emit, format!("cannot create the output folder: {e}")); }
    let v1 = a.index_v1;
    let (mut items, mut skipped): (Vec<Item>, u64) = (Vec::new(), 0);
    let mut used: std::collections::HashSet<String> = Default::default();
    let mut old: Vec<(String, Item)> = Vec::new(); // (lowercase file name, item) in index order
    if a.merge && index_dir.join(&a.index_name).is_file() {
        let r = std::fs::read(index_dir.join(&a.index_name)).map_err(|e| e.to_string()).and_then(|d| index::parse_index(&d)).and_then(|(ver, ents)| {
            if ver == 1 && !v1 { return Err("the old index is version 1 and --index-format v0 cannot hold folders".to_string()); }
            let mut v = Vec::new();
            for e in ents {
                let fname = Path::new(&e.path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
                if !photos.join(&fname).is_file() { continue; }
                let thumb = if e.version == 1 { e.thumb } else if v1 { index::thumb72_to_square(&e.thumb) } else { e.thumb };
                v.push((fname.to_lowercase(), Item { path: e.path, name: e.name, size: e.size, width: e.width, height: e.height, folder_name: e.folder_name, taken: e.taken, details: e.details, thumb, order: v.len() }));
            }
            Ok(v)
        });
        match r { Ok(v) => old = v, Err(e) => emit(json!({"event": "warning", "error": format!("the old index is not readable and is rebuilt without it: {e}")})) }
    }
    emit(json!({"event": "plan", "files": jobs.len(), "kept": old.len()}));
    for (p, folder) in &jobs {
        if cancel.load(Ordering::Relaxed) { emit(json!({"event": "error", "error": "Stopped."})); return 2; }
        let stem0 = p.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        let fname = p.file_name().unwrap().to_string_lossy().into_owned();
        let base = ascii_stem(&stem0);
        let (mut stem, mut n) = (base.clone(), 1);
        while used.contains(&stem.to_lowercase()) { n += 1; stem = format!("{base}_{n}"); }
        let shown = if n == 1 { stem0.clone() } else { format!("{stem0}_{n}") };
        let path = format!("{card_dir}/{stem}.tgy");
        if path.len() >= index::PATH_BYTES {
            emit(json!({"event": "skip", "file": fname, "error": format!("the card path is {} bytes, the limit is {}: shorten the file name or the card folder", path.len(), index::PATH_BYTES - 1)}));
            skipped += 1; continue;
        }
        let prep = match std::fs::read(p).map_err(|e| e.to_string()).and_then(|d| picture::prepare(&d)) { Ok(x) => x, Err(e) => { emit(json!({"event": "skip", "file": fname, "error": e})); skipped += 1; continue; } };
        let (canvas, rect, thumb) = render(&prep, a.upscale);
        let levels = if a.levels && !a.format_v1 { zoom_levels(&prep) } else { vec![] };
        let sq = if v1 { square_thumb(&prep) } else { vec![] };
        used.insert(stem.to_lowercase());
        let data = if a.format_v1 {
            match container_v1(&prep, &canvas, a.quality, a.fit_quality, &thumb, cancel) {
                Ok(d) => d,
                Err(e) => { if e == "Stopped." { emit(json!({"event": "error", "error": e})); return 2; } emit(json!({"event": "skip", "file": fname, "error": e})); skipped += 1; used.remove(&stem.to_lowercase()); continue; }
            }
        } else { pack_tgy_v0(&canvas, rect, &thumb, (prep.w, prep.h), &levels) };
        if let Err(e) = write_atomic(&photos.join(format!("{stem}.tgy")), &data) { return err(emit, format!("cannot write {stem}.tgy: {e}")); }
        old.retain(|(k, _)| *k != format!("{stem}.tgy").to_lowercase());
        let folder_name = folder.clone().unwrap_or_else(|| index::ALL_PICTURES.to_string());
        let order = old.len() + items.len();
        emit(json!({"event": "converted", "file": fname, "tgy": format!("{stem}.tgy"), "size": data.len(), "width": prep.w, "height": prep.h, "rect": [rect.0, rect.1, rect.2, rect.3], "folder": folder_name,
            "levels": levels.iter().map(|l| match l { ZoomLevel::Raster { w, h, .. } => json!([w, h, "raster"]), ZoomLevel::Tiles { w, h, .. } => json!([w, h, "tiles"]) }).collect::<Vec<_>>()}));
        items.push(Item { path, name: name_of(&shown), size: data.len() as u32, width: prep.w as u32, height: prep.h as u32, folder_name, taken: taken_seconds(prep.details.taken), details: index::pack_details(&prep.details), thumb: if v1 { sq } else { thumb }, order });
    }
    let mut all: Vec<Item> = old.into_iter().map(|(_, i)| i).collect();
    all.extend(items.iter().map(|i| Item { path: i.path.clone(), name: i.name.clone(), size: i.size, width: i.width, height: i.height, folder_name: i.folder_name.clone(), taken: i.taken, details: i.details.clone(), thumb: i.thumb.clone(), order: i.order }));
    let (pictures, folders_n);
    if v1 {
        let mut fnames: Vec<String> = all.iter().map(|i| i.folder_name.clone()).collect::<std::collections::BTreeSet<_>>().into_iter().collect();
        fnames.sort_by_key(|x| x.to_lowercase());
        let mut ordered: Vec<&Item> = Vec::new();
        for f in &fnames {
            let mut v: Vec<&Item> = all.iter().filter(|i| &i.folder_name == f).collect();
            match a.sort.as_str() {
                "taken" => v.sort_by_key(|i| (i.taken == 0, i.taken, i.path.to_lowercase())),
                "converted" => v.sort_by_key(|i| i.order),
                _ => v.sort_by_key(|i| (i.name.to_lowercase(), i.path.to_lowercase())),
            }
            ordered.extend(v);
        }
        let folders: Vec<index::Folder> = fnames.iter().map(|f| index::Folder { name: f.chars().map(|c| if (c as u32) < 256 { c } else { '?' }).take(index::FOLDER_NAME - 1).collect() }).collect();
        let entries: Vec<index::Entry1> = ordered.iter().map(|i| index::Entry1 { path: i.path.clone(), name: i.name.clone(), size: i.size, width: i.width, height: i.height, folder: fnames.iter().position(|f| *f == i.folder_name).unwrap() as u16, taken: i.taken, details: i.details.clone(), thumb: i.thumb.clone() }).collect();
        let fl = if folders.is_empty() { vec![index::Folder { name: index::ALL_PICTURES.into() }] } else { folders };
        let sort = match a.sort.as_str() { "taken" => index::SORT_TAKEN, "converted" => index::SORT_CONVERTED, _ => index::SORT_NAME };
        match index::pack_index_v1(&entries, &fl, sort) { Ok(d) => { if let Err(e) = write_atomic(&index_dir.join(&a.index_name), &d) { return err(emit, format!("cannot write the index: {e}")); } } Err(e) => return err(emit, e) }
        pictures = ordered.len(); folders_n = fl.len();
    } else {
        let mut ordered: Vec<&Item> = all.iter().collect();
        if ordered.len() > items.len() { ordered.sort_by_key(|i| i.path.to_lowercase()); }
        let entries: Vec<index::Entry0> = ordered.iter().map(|i| index::Entry0 { path: i.path.clone(), name: i.name.clone(), size: i.size, width: i.width, height: i.height, thumb: i.thumb.clone() }).collect();
        match index::pack_index_v0(&entries) { Ok(d) => { if let Err(e) = write_atomic(&index_dir.join(&a.index_name), &d) { return err(emit, format!("cannot write the index: {e}")); } } Err(e) => return err(emit, e) }
        pictures = ordered.len(); folders_n = 1;
    }
    emit(json!({"event": "done", "pictures": pictures, "skipped": skipped, "converted": items.len(), "folders": folders_n, "out": a.out.to_string_lossy()}));
    0
}

#[cfg(test)]
mod nested_tests {
    use super::*;
    fn touch(p: &Path) { std::fs::create_dir_all(p.parent().unwrap()).unwrap(); std::fs::write(p, b"x").unwrap(); }
    #[test]
    fn nested_folders_flatten_or_join() {
        let d = std::env::temp_dir().join(format!("tgy_nested_{}", std::process::id())); let _ = std::fs::remove_dir_all(&d);
        touch(&d.join("Trip/t.jpg")); touch(&d.join("Trip/Day 1/a.jpg")); touch(&d.join("Trip/Day 2/Deep/b.jpg")); touch(&d.join("Trip/.hid/h.jpg")); touch(&d.join("Empty/readme.txt")); touch(&d.join("top.jpg"));
        let list = |p: &Path| -> Vec<PathBuf> { let mut v: Vec<PathBuf> = std::fs::read_dir(p).map(|rd| rd.filter_map(|e| e.ok()).map(|e| e.path()).filter(|q| q.is_file() && q.extension().map(|e| e == "jpg").unwrap_or(false)).collect()).unwrap_or_default(); v.sort(); v };
        let names = |v: &Vec<(String, Vec<PathBuf>)>| v.iter().map(|(n, f)| format!("{}:{}", n, f.len())).collect::<Vec<_>>();
        let f = nested_folders(&d, "flatten", &list); assert_eq!(names(&f), vec!["Trip:3"], "flatten: one folder, every depth, the hidden folder and the folder without pictures skipped");
        let j = nested_folders(&d, "join", &list); assert_eq!(names(&j), vec!["Trip:1", "Trip - Day 1:1", "Trip - Day 2 - Deep:1"], "join: a folder of its own for each, named with the path");
        let _ = std::fs::remove_dir_all(&d);
    }
}
