//! TGY container, version 1 (docs/TGY_CONTAINER_V1.md): the writer, a port of `tools/gallery/tgy_container.py::write_container`. One file per picture:
//! a header, a level table, the quantisation tables, a thumbnail and levels; every level is a grid of independent 128 x 128 tiles, each tile one
//! symbol-layer stream with its own CRC-32 in a tile index. Sections start on 4 KB boundaries. Everything little-endian.
use crate::jpegcoef::Jpeg;
use crate::recon;
use crate::v0;
use rayon::prelude::*;

pub const MAGIC: &[u8; 4] = b"TGY\0";
pub const VERSION: u16 = 1;
const HEADER: usize = 64;
const LEVEL: usize = 48;
const ALIGN: usize = 4096;
pub const KIND_FULL: u8 = 1;
pub const KIND_FIT: u8 = 2;
pub const THUMB_BYTES: usize = 96 * 72 * 2;
const MAX_TABLES: usize = 8;
pub const SUB_GREY: u8 = 0;
pub const SUB_444: u8 = 1;
pub const SUB_420: u8 = 2;

fn crc(b: &[u8]) -> u32 { crc32fast::hash(b) }
fn align(n: usize) -> usize { n.div_ceil(ALIGN) * ALIGN }

pub fn sub_of(p: &Jpeg) -> u8 {
    if p.components.len() == 1 { SUB_GREY } else if (p.components[0].h, p.components[0].v) == (2, 2) { SUB_420 } else { SUB_444 }
}

pub struct Level<'a> { pub kind: u8, pub pic: &'a Jpeg, pub rect: (u16, u16, u16, u16) }

fn put32(b: &mut [u8], o: usize, v: u32) { b[o..o + 4].copy_from_slice(&v.to_le_bytes()); }

/// The container bytes. `levels` in file order (the fit level first). Tiles are coded on all cores.
pub fn write_container(width: u32, height: u32, orientation: u8, thumb565: &[u8], levels: &[Level], cancel: &std::sync::atomic::AtomicBool) -> Result<Vec<u8>, String> {
    if thumb565.len() != THUMB_BYTES || levels.is_empty() || levels.len() > 16 { return Err("bad container arguments".into()); }
    struct Entry { w: u32, h: u32, rect: (u16, u16, u16, u16), pixfmt: u8, kind: u8, qy: u8, qc: u8, data_off: u32, data_len: u32, index_off: u32, index_len: u32, index_crc: u32 }
    let mut tables: Vec<[u16; 64]> = Vec::new();
    let mut entries: Vec<Entry> = Vec::new();
    let mut blobs: Vec<(Vec<u8>, Vec<u8>)> = Vec::new();
    for lv in levels {
        let pic = lv.pic;
        let sub = sub_of(pic);
        let mut e = Entry { w: pic.width as u32, h: pic.height as u32, rect: lv.rect, pixfmt: sub, kind: lv.kind, qy: 0, qc: 0, data_off: 0, data_len: 0, index_off: 0, index_len: 0, index_crc: 0 };
        for (slot, comp_i) in [(0usize, 0usize), (1usize, 1usize)] {
            if slot == 1 && sub == SUB_GREY { continue; }
            let t = pic.quant[pic.components[comp_i].tq].ok_or("missing quantisation table")?;
            if sub != SUB_GREY && slot == 1 && pic.components[2].tq != pic.components[1].tq { return Err("the two chroma components use different quantisation tables".into()); }
            let idx = match tables.iter().position(|x| *x == t) { Some(i) => i, None => { tables.push(t); tables.len() - 1 } };
            if slot == 0 { e.qy = idx as u8 } else { e.qc = idx as u8 }
        }
        if tables.len() > MAX_TABLES { return Err("more than 8 quantisation tables".into()); }
        let (rows, cols) = recon::tile_dims(pic);
        let streams: Vec<Vec<u8>> = (0..rows * cols).into_par_iter().map(|i| if cancel.load(std::sync::atomic::Ordering::Relaxed) { Vec::new() } else { v0::encode_tile(&recon::tile_grids(pic, i / cols, i % cols)) }).collect();
        if cancel.load(std::sync::atomic::Ordering::Relaxed) { return Err("Stopped.".into()); }
        let mut index = Vec::with_capacity(streams.len() * 12);
        let mut pos = 0u32;
        let mut data = Vec::with_capacity(streams.iter().map(|s| s.len()).sum());
        for s in &streams {
            index.extend_from_slice(&pos.to_le_bytes());
            index.extend_from_slice(&(s.len() as u32).to_le_bytes());
            index.extend_from_slice(&crc(s).to_le_bytes());
            pos += s.len() as u32;
            data.extend_from_slice(s);
        }
        e.index_crc = crc(&index);
        entries.push(e);
        blobs.push((index, data));
    }
    let mut tables_blob = (tables.len() as u16).to_le_bytes().to_vec();
    for t in &tables { for v in t { tables_blob.extend_from_slice(&v.to_le_bytes()); } }
    let nlev = levels.len();
    let lt_off = HEADER;
    let tab_off = lt_off + LEVEL * nlev;
    let thumb_off = align(tab_off + tables_blob.len());
    let mut pos = thumb_off + align(THUMB_BYTES);
    for (e, (index, data)) in entries.iter_mut().zip(&blobs) {
        if !index.is_empty() { e.index_off = pos as u32; e.index_len = index.len() as u32; pos = align(pos + index.len()); }
        e.data_off = pos as u32;
        e.data_len = data.len() as u32;
        pos = align(pos + data.len());
    }
    let size = pos;
    if size > u32::MAX as usize { return Err("the file would be larger than 4 GB".into()); }
    let mut buf = vec![0u8; size];
    let channels: u8 = if levels.iter().any(|l| l.pic.components.len() == 1) { 1 } else { 3 };
    let mut head = Vec::with_capacity(HEADER);
    head.extend_from_slice(MAGIC);
    head.extend_from_slice(&VERSION.to_le_bytes());
    head.extend_from_slice(&(HEADER as u16).to_le_bytes());
    head.extend_from_slice(&width.to_le_bytes());
    head.extend_from_slice(&height.to_le_bytes());
    head.extend_from_slice(&[channels, 0, orientation, 0]);
    for v in [nlev as u32, lt_off as u32, thumb_off as u32, THUMB_BYTES as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    head.extend_from_slice(&96u16.to_le_bytes());
    head.extend_from_slice(&72u16.to_le_bytes());
    for v in [tab_off as u32, tables_blob.len() as u32, size as u32] { head.extend_from_slice(&v.to_le_bytes()); }
    let c = crc(&head);
    head.extend_from_slice(&c.to_le_bytes());
    head.resize(HEADER, 0);
    buf[..HEADER].copy_from_slice(&head);
    for (i, e) in entries.iter().enumerate() {
        let o = lt_off + i * LEVEL;
        put32(&mut buf, o, e.w);
        put32(&mut buf, o + 4, e.h);
        for (k, v) in [e.rect.0, e.rect.1, e.rect.2, e.rect.3].iter().enumerate() { buf[o + 8 + 2 * k..o + 10 + 2 * k].copy_from_slice(&v.to_le_bytes()); }
        buf[o + 16..o + 24].copy_from_slice(&[1, 1, e.pixfmt, e.kind, e.qy, e.qc, 7, 0]); // layout tiles, mode TGY, ..., tile_log2 7, flags 0
        put32(&mut buf, o + 24, e.data_off);
        put32(&mut buf, o + 28, e.data_len);
        put32(&mut buf, o + 32, e.index_off);
        put32(&mut buf, o + 36, e.index_len);
        put32(&mut buf, o + 40, e.index_crc);
    }
    buf[tab_off..tab_off + tables_blob.len()].copy_from_slice(&tables_blob);
    buf[thumb_off..thumb_off + THUMB_BYTES].copy_from_slice(thumb565);
    for (e, (index, data)) in entries.iter().zip(&blobs) {
        buf[e.index_off as usize..e.index_off as usize + index.len()].copy_from_slice(index);
        buf[e.data_off as usize..e.data_off as usize + data.len()].copy_from_slice(data);
    }
    Ok(buf)
}
