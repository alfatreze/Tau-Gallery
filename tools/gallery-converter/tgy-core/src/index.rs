//! The library index: TGIX version 1 (docs/features/TGIX_V1_SPEC.md: folders, per-picture details, 96 x 96 thumbnails, a sort key) and the old flat
//! version 0 (96 x 72 thumbnails). Writer and reader, ports of `tools/gallery/tgy.py` (pack_index_v1, pack_index, parse_index_full).
use crate::picture::Details;

pub const ALIGN: usize = 4096;
pub const PATH_BYTES: usize = 96;
pub const NAME_BYTES: usize = 40;
pub const THUMB_W: usize = 96;
pub const THUMB_H: usize = 72;
pub const THUMB_BYTES: usize = THUMB_W * THUMB_H * 2;
pub const THUMB1_BYTES: usize = 96 * 96 * 2;
const IDX_HEADER: usize = 32;
const IDX_ENTRY: usize = 160;
const IDX1_HEADER: usize = 64;
const IDX1_ENTRY: usize = 176;
const FOLDER_ENTRY: usize = 48;
pub const FOLDER_NAME: usize = 32;
const MAX_ENTRIES: usize = 65535;
const MAX_FOLDERS: usize = 255;
const MAX_DETAILS: usize = 255;
pub const SORT_NAME: u16 = 0;
pub const SORT_TAKEN: u16 = 1;
pub const SORT_CONVERTED: u16 = 2;
pub const ALL_PICTURES: &str = "ALL PICTURES";

fn align(n: usize) -> usize { n.div_ceil(ALIGN) * ALIGN }
fn crc(b: &[u8]) -> u32 { crc32fast::hash(b) }

/// Latin-1 with '?' for what does not fit (Python's encode("latin-1", "replace")).
pub fn latin1(s: &str) -> Vec<u8> { s.chars().map(|c| if (c as u32) < 256 { c as u32 as u8 } else { b'?' }).collect() }
fn from_latin1(b: &[u8]) -> String { b.iter().map(|&c| c as char).collect() }

pub fn pack_details(d: &Details) -> Vec<u8> {
    let mut out = Vec::new();
    let mut item = |tag: u8, b: &[u8]| { out.push(tag); out.push(b.len() as u8); out.extend_from_slice(b); };
    if !d.format.is_empty() { let f: Vec<u8> = d.format.bytes().map(|c| if c < 128 { c } else { b'?' }).take(15).collect(); item(1, &f); }
    if let Some(c) = &d.camera { let b = latin1(c); item(2, &b[..b.len().min(40)]); }
    if let Some(c) = &d.lens { let b = latin1(c); item(3, &b[..b.len().min(40)]); }
    if let Some((y, mo, dd, h, mi, s)) = d.taken { let mut b = y.to_le_bytes().to_vec(); b.extend_from_slice(&[mo, dd, h, mi, s]); item(4, &b); }
    if let Some((n, de)) = d.shutter { let mut b = n.to_le_bytes().to_vec(); b.extend_from_slice(&de.to_le_bytes()); item(5, &b); }
    if let Some(a) = d.aperture { if a != 0 { item(6, &a.to_le_bytes()); } }
    if let Some(i) = d.iso { if i != 0 { item(7, &i.to_le_bytes()); } }
    if let Some(f) = d.focal { if f != 0 { item(8, &f.to_le_bytes()); } }
    out
}

pub struct Entry1 {
    pub path: String,
    pub name: String,
    pub size: u32,
    pub width: u32,
    pub height: u32,
    pub folder: u16,
    pub taken: u32,
    pub details: Vec<u8>,
    pub thumb: Vec<u8>,
}

/// A folder of the table. `parent` is the index of the folder it is in (None: top level); the table is in depth first order, so a folder's descendants follow it (TGIX 1.1).
pub struct Folder { pub name: String, pub parent: Option<usize> }

fn put(buf: &mut [u8], o: usize, b: &[u8]) { buf[o..o + b.len()].copy_from_slice(b); }

pub fn pack_index_v1(entries: &[Entry1], folders: &[Folder], sort: u16) -> Result<Vec<u8>, String> {
    let n = entries.len();
    if n > MAX_ENTRIES { return Err("too many pictures for one library".into()); }
    if folders.is_empty() || folders.len() > MAX_FOLDERS { return Err("folder count".into()); }
    let fi: Vec<u16> = entries.iter().map(|e| e.folder).collect();
    if fi.iter().any(|&f| f as usize >= folders.len()) || fi.windows(2).any(|w| w[0] > w[1]) { return Err("entries must be sorted by folder".into()); }
    let nf = folders.len();
    if folders.iter().enumerate().any(|(k, f)| f.parent.map_or(false, |p| p >= k)) { return Err("a folder's parent must come before it".into()); }
    let mut nfold = vec![0u16; nf];
    for k in 0..nf { let mut a = folders[k].parent; while let Some(p) = a { nfold[p] += 1; a = folders[p].parent; } }
    for k in 0..nf { for j in k + 1..k + 1 + nfold[k] as usize {                                          // the descendants are exactly the next nfold folders (depth first order)
        let mut a = if j < nf { folders[j].parent } else { None }; while let Some(p) = a { if p == k { break; } a = folders[p].parent; }
        if a != Some(k) { return Err("folders are not in depth first order".into()); } } }
    let own: Vec<u32> = (0..nf).map(|k| fi.iter().filter(|&&f| f as usize == k).count() as u32).collect();
    if (0..nf).any(|k| nfold[k] > 0 && own[k] > 0) { return Err("a folder that holds folders cannot hold pictures: put them in a folder of their own".into()); }
    if n > 0 && (0..nf).any(|k| nfold[k] == 0 && own[k] == 0) { return Err("a folder with no picture".into()); }
    let mut first: Vec<i64> = (0..nf).map(|k| fi.iter().position(|&f| f as usize == k).map(|p| p as i64).unwrap_or(-1)).collect();
    let mut count: Vec<u32> = own.clone();
    for k in (0..nf).rev() { if nfold[k] > 0 { let r = k + 1..k + 1 + nfold[k] as usize; first[k] = if n > 0 { r.clone().filter_map(|j| if first[j] >= 0 { Some(first[j]) } else { None }).min().unwrap_or(0) } else { 0 }; count[k] = r.map(|j| own[j]).sum(); } }
    let mut det = Vec::new();
    let mut recs = Vec::new();
    for e in entries {
        if e.details.len() > MAX_DETAILS { return Err("details too long".into()); }
        recs.push((det.len() as u32, e.details.len() as u16));
        det.extend_from_slice(&e.details);
    }
    let ent_off = IDX1_HEADER;
    let atlas_off = align(ent_off + n * IDX1_ENTRY);
    let fold_off = align(atlas_off + n * THUMB1_BYTES);
    let det_off = fold_off + folders.len() * FOLDER_ENTRY;
    let total = align(det_off + det.len() + 1);
    let mut buf = vec![0u8; total];
    let mut head = Vec::new();
    head.extend_from_slice(b"TGIX");
    head.extend_from_slice(&1u16.to_le_bytes());
    head.extend_from_slice(&(IDX1_HEADER as u16).to_le_bytes());
    head.extend_from_slice(&(n as u32).to_le_bytes());
    head.extend_from_slice(&(IDX1_ENTRY as u16).to_le_bytes());
    head.extend_from_slice(&96u16.to_le_bytes());
    head.extend_from_slice(&96u16.to_le_bytes());
    head.extend_from_slice(&(folders.len() as u16).to_le_bytes());
    for v in [ent_off as u32, atlas_off as u32, fold_off as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    head.extend_from_slice(&(FOLDER_ENTRY as u16).to_le_bytes());
    head.extend_from_slice(&sort.to_le_bytes());
    head.extend_from_slice(&(det_off as u32).to_le_bytes());
    head.extend_from_slice(&(det.len() as u32).to_le_bytes());
    head.extend_from_slice(&0u32.to_le_bytes());
    head.resize(48, 0);
    head.resize(60, 0);
    buf[..60].copy_from_slice(&head);
    let c = crc(&buf[..60]);
    put(&mut buf, 60, &c.to_le_bytes());
    for (i, e) in entries.iter().enumerate() {
        let p = e.path.as_bytes();
        if p.len() >= PATH_BYTES { return Err(format!("path too long for the index: {}", e.path)); }
        if e.thumb.len() != THUMB1_BYTES { return Err("thumbnail size".into()); }
        let nm = latin1(&e.name);
        let mut rec = vec![0u8; IDX1_ENTRY];
        rec[..p.len()].copy_from_slice(p);
        let nm = &nm[..nm.len().min(NAME_BYTES - 1)];
        rec[PATH_BYTES..PATH_BYTES + nm.len()].copy_from_slice(nm);
        let mut o = PATH_BYTES + NAME_BYTES;
        for v in [e.size, e.width, e.height, i as u32] { rec[o..o + 4].copy_from_slice(&v.to_le_bytes()); o += 4; }
        let (do_, dl) = recs[i];
        rec[o..o + 2].copy_from_slice(&e.folder.to_le_bytes());
        rec[o + 2..o + 4].copy_from_slice(&(if dl != 0 { 1u16 } else { 0 }).to_le_bytes());
        rec[o + 4..o + 8].copy_from_slice(&do_.to_le_bytes());
        rec[o + 8..o + 10].copy_from_slice(&dl.to_le_bytes());
        rec[o + 10..o + 12].copy_from_slice(&0u16.to_le_bytes());
        rec[o + 12..o + 16].copy_from_slice(&e.taken.to_le_bytes());
        put(&mut buf, ent_off + i * IDX1_ENTRY, &rec);
        put(&mut buf, atlas_off + i * THUMB1_BYTES, &e.thumb);
    }
    for (k, f) in folders.iter().enumerate() {
        let nm = latin1(&f.name);
        let nm = &nm[..nm.len().min(FOLDER_NAME - 1)];
        let mut rec = vec![0u8; FOLDER_ENTRY];
        rec[..nm.len()].copy_from_slice(nm);
        let fst = first[k].max(0) as u32;
        rec[FOLDER_NAME..FOLDER_NAME + 4].copy_from_slice(&fst.to_le_bytes());
        rec[FOLDER_NAME + 4..FOLDER_NAME + 8].copy_from_slice(&count[k].to_le_bytes());
        rec[FOLDER_NAME + 8..FOLDER_NAME + 12].copy_from_slice(&fst.to_le_bytes());
        rec[FOLDER_NAME + 12..FOLDER_NAME + 14].copy_from_slice(&(f.parent.map_or(0, |p| p as u16 + 1)).to_le_bytes());
        rec[FOLDER_NAME + 14..FOLDER_NAME + 16].copy_from_slice(&nfold[k].to_le_bytes());
        put(&mut buf, fold_off + k * FOLDER_ENTRY, &rec);
    }
    put(&mut buf, det_off, &det);
    Ok(buf)
}

pub struct Entry0 { pub path: String, pub name: String, pub size: u32, pub width: u32, pub height: u32, pub thumb: Vec<u8> }

pub fn pack_index_v0(entries: &[Entry0]) -> Result<Vec<u8>, String> {
    let n = entries.len();
    let ent_off = IDX_HEADER;
    let atlas_off = align(ent_off + n * IDX_ENTRY);
    let mut buf = vec![0u8; align(atlas_off + n * THUMB_BYTES)];
    let mut head = Vec::new();
    head.extend_from_slice(b"TGIX");
    head.extend_from_slice(&0u16.to_le_bytes());
    head.extend_from_slice(&(IDX_HEADER as u16).to_le_bytes());
    for v in [n as u32, IDX_ENTRY as u32, ent_off as u32, atlas_off as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    head.extend_from_slice(&(THUMB_W as u16).to_le_bytes());
    head.extend_from_slice(&(THUMB_H as u16).to_le_bytes());
    head.extend_from_slice(&0u32.to_le_bytes());
    buf[..IDX_HEADER].copy_from_slice(&head);
    for (i, e) in entries.iter().enumerate() {
        let p = e.path.as_bytes();
        if p.len() >= PATH_BYTES { return Err(format!("path too long for the index: {}", e.path)); }
        if e.thumb.len() != THUMB_BYTES { return Err("thumbnail size".into()); }
        let nm = latin1(&e.name);
        let nm = &nm[..nm.len().min(NAME_BYTES - 1)];
        let mut rec = vec![0u8; IDX_ENTRY];
        rec[..p.len()].copy_from_slice(p);
        rec[PATH_BYTES..PATH_BYTES + nm.len()].copy_from_slice(nm);
        let mut o = PATH_BYTES + NAME_BYTES;
        for v in [e.size, e.width, e.height, i as u32] { rec[o..o + 4].copy_from_slice(&v.to_le_bytes()); o += 4; }
        put(&mut buf, ent_off + i * IDX_ENTRY, &rec);
        put(&mut buf, atlas_off + i * THUMB_BYTES, &e.thumb);
    }
    Ok(buf)
}

/// An index read back for --merge: both versions, as the entries the converter keeps.
pub struct Old { pub path: String, pub name: String, pub size: u32, pub width: u32, pub height: u32, pub folder_name: String, pub taken: u32, pub details: Vec<u8>, pub thumb: Vec<u8>, pub version: u16 }

fn rd32(d: &[u8], o: usize) -> Result<u32, String> { d.get(o..o + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]])).ok_or_else(|| "index out of range".to_string()) }
fn rd16(d: &[u8], o: usize) -> Result<u16, String> { d.get(o..o + 2).map(|b| u16::from_le_bytes([b[0], b[1]])).ok_or_else(|| "index out of range".to_string()) }
fn cstr(b: &[u8]) -> &[u8] { b.split(|&c| c == 0).next().unwrap_or(&[]) }

/// A 96 x 72 thumbnail padded to 96 x 96 with black bars (12 rows above and below).
pub fn thumb72_to_square(t: &[u8]) -> Vec<u8> {
    let mut o = vec![0u8; 12 * 96 * 2];
    o.extend_from_slice(t);
    o.extend(std::iter::repeat(0u8).take(12 * 96 * 2));
    o
}

pub fn parse_index(d: &[u8]) -> Result<(u16, Vec<Old>), String> {
    if d.len() < 32 || &d[..4] != b"TGIX" { return Err("not a TGIX file".into()); }
    let ver = rd16(d, 4)?;
    if ver == 0 {
        if rd16(d, 6)? as usize != IDX_HEADER || rd32(d, 12)? as usize != IDX_ENTRY || rd16(d, 24)? as usize != THUMB_W || rd16(d, 26)? as usize != THUMB_H { return Err("unknown TGIX version".into()); }
        let (n, eoff, aoff) = (rd32(d, 8)? as usize, rd32(d, 16)? as usize, rd32(d, 20)? as usize);
        let mut out = Vec::new();
        for i in 0..n {
            let r = d.get(eoff + i * IDX_ENTRY..eoff + (i + 1) * IDX_ENTRY).ok_or("index out of range")?;
            let ti = rd32(r, PATH_BYTES + NAME_BYTES + 12)? as usize;
            out.push(Old { path: String::from_utf8(cstr(&r[..PATH_BYTES]).to_vec()).map_err(|_| "bad path")?, name: from_latin1(cstr(&r[PATH_BYTES..PATH_BYTES + NAME_BYTES])), size: rd32(r, PATH_BYTES + NAME_BYTES)?, width: rd32(r, PATH_BYTES + NAME_BYTES + 4)?, height: rd32(r, PATH_BYTES + NAME_BYTES + 8)?,
                folder_name: ALL_PICTURES.into(), taken: 0, details: vec![], thumb: d.get(aoff + ti * THUMB_BYTES..aoff + (ti + 1) * THUMB_BYTES).ok_or("index out of range")?.to_vec(), version: 0 });
        }
        return Ok((0, out));
    }
    if ver != 1 || d.len() < IDX1_HEADER { return Err("unknown TGIX version".into()); }
    if crc(&d[..60]) != rd32(d, 60)? { return Err("header checksum".into()); }
    let (n, nf, eoff, aoff, foff, doff, dlen) = (rd32(d, 8)? as usize, rd16(d, 18)? as usize, rd32(d, 20)? as usize, rd32(d, 24)? as usize, rd32(d, 28)? as usize, rd32(d, 36)? as usize, rd32(d, 40)? as usize);
    if rd16(d, 6)? as usize != IDX1_HEADER || rd16(d, 12)? as usize != IDX1_ENTRY || rd16(d, 32)? as usize != FOLDER_ENTRY || rd16(d, 14)? != 96 || rd16(d, 16)? != 96 || nf == 0 || nf > MAX_FOLDERS || n > MAX_ENTRIES || rd16(d, 34)? > 2 { return Err("TGIX header fields".into()); }
    if eoff < IDX1_HEADER || aoff < eoff + n * IDX1_ENTRY || foff < aoff + n * THUMB1_BYTES || doff < foff + nf * FOLDER_ENTRY || doff + dlen > d.len() { return Err("TGIX sections".into()); }
    let mut names = Vec::new();
    for k in 0..nf { let r = d.get(foff + k * FOLDER_ENTRY..foff + (k + 1) * FOLDER_ENTRY).ok_or("index out of range")?; names.push(from_latin1(cstr(&r[..FOLDER_NAME]))); }
    let mut out = Vec::new();
    for i in 0..n {
        let r = d.get(eoff + i * IDX1_ENTRY..eoff + (i + 1) * IDX1_ENTRY).ok_or("index out of range")?;
        let o = PATH_BYTES + NAME_BYTES;
        let (size, w, h, ti, fo, _fl, do_, dl, taken) = (rd32(r, o)?, rd32(r, o + 4)?, rd32(r, o + 8)?, rd32(r, o + 12)? as usize, rd16(r, o + 16)? as usize, rd16(r, o + 18)?, rd32(r, o + 20)? as usize, rd16(r, o + 24)? as usize, rd32(r, o + 28)?);
        if ti >= n || fo >= nf || do_ + dl > dlen { return Err("entry fields".into()); }
        out.push(Old { path: String::from_utf8(cstr(&r[..PATH_BYTES]).to_vec()).map_err(|_| "bad path")?, name: from_latin1(cstr(&r[PATH_BYTES..PATH_BYTES + NAME_BYTES])), size, width: w, height: h, folder_name: names[fo].clone(), taken,
            details: d[doff + do_..doff + do_ + dl].to_vec(), thumb: d.get(aoff + ti * THUMB1_BYTES..aoff + (ti + 1) * THUMB1_BYTES).ok_or("index out of range")?.to_vec(), version: 1 });
    }
    Ok((1, out))
}
