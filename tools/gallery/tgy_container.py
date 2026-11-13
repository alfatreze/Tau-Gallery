"""TGY container, version 1 (docs/TGY_CONTAINER_V1.md): reference writer and reader. One file per picture: a header, a level table, the quantisation tables, a thumbnail and levels. A level is either a RAW raster canvas
(as container version 0) or a grid of independent 128 x 128 tiles, each tile one TGY symbol layer v0 stream (docs/TGY_SYMBOL_LAYER_V0.md) with its own CRC-32 in a tile index; pixels come from the reconstruction
(docs/TGY_RECONSTRUCTION_V0.md). Sections start on 4 KB boundaries; every offset, length and count read from a file is bounds-checked before use. Everything little-endian."""
import struct, zlib
import numpy as np
import jpegcoef, tgy_recon as R, tgy_v0 as V

MAGIC, VERSION, HEADER, LEVEL, ALIGN, TILE_INDEX = b"TGY\0", 1, 64, 48, 4096, 12
LAYOUT_RASTER, LAYOUT_TILES = 0, 1
MODE_RAW, MODE_TGY = 0, 1                                                                                 # 2 is reserved for TGX
KIND_FIT_RAW, KIND_FULL, KIND_FIT = 0, 1, 2
SUB_GREY, SUB_444, SUB_420 = 0, 1, 2
CANVAS_W, CANVAS_H, THUMB_BYTES = 800, 720, 96 * 72 * 2
MAX_LEVELS, MAX_TABLES = 16, 8
crc = lambda b: zlib.crc32(b) & 0xFFFFFFFF
align = lambda n: (n + ALIGN - 1) // ALIGN * ALIGN

def sub_of(pic):
    if len(pic.components) == 1: return SUB_GREY
    return SUB_420 if (pic.components[0].h, pic.components[0].v) == (2, 2) else SUB_444

def comp_grids(w, h, sub):
    """Blocks per component (blocks_w, blocks_h), padded to whole MCUs, and the sampling factors (h, v) of each component."""
    if sub == SUB_GREY: return [(-(-w // 8), -(-h // 8), 1, 1)]
    mcu = 16 if sub == SUB_420 else 8; mw, mh = -(-w // mcu), -(-h // mcu); f = 2 if sub == SUB_420 else 1
    return [(mw * f, mh * f, f, f), (mw, mh, 1, 1), (mw, mh, 1, 1)]

def blank_picture(w, h, sub, tables):
    """A coefficient picture with the geometry of (w, h, sub) and all-zero coefficients; tables = [quantisation table of Y, of the chroma] (zigzag lists)."""
    j = jpegcoef.Jpeg(); j.width, j.height, j.progressive, j.restart_interval, j.quant, j.components = w, h, False, 0, {}, []
    for k, (bw, bh, hf, vf) in enumerate(comp_grids(w, h, sub)):
        c = jpegcoef.Comp(k + 1, hf, vf, 0 if k == 0 else 1); c.blocks_w, c.blocks_h = bw, bh; c.coef = np.zeros((bh, bw, 64), dtype=np.int16); j.components.append(c)
    for k, t in enumerate(tables[:2 if sub != SUB_GREY else 1]): j.quant[k] = np.array(t)
    return j

def tile_grid(w, h, sub):
    """(tile rows, tile columns) of a level of w x h samples."""
    bw, bh = comp_grids(w, h, sub)[0][:2]                                                                   # a tile is 16 x 16 blocks of Y in every layout
    return -(-bh // 16), -(-bw // 16)

def pack_level(e):
    return struct.pack("<IIHHHHBBBBBBBBIIIIII", e["w"], e["h"], *e["rect"], e["layout"], e["mode"], e["pixfmt"], e["kind"], e["qy"], e["qc"], e["tile_log2"], e["flags"], e["data_off"], e["data_len"], e["index_off"], e["index_len"], e["index_crc"], 0)

def _encode_one(t): return V.encode_tile(t[0], t[1])

def auto_jobs():
    """The processes the encoder uses when asked for 'automatic' (0): the machine's cores up to 8 (more gain little: the picture's decode and the file assembly stay serial)."""
    import os
    return max(1, min(os.cpu_count() or 1, 8))

def encode_tiles(tiles, jobs=1):
    """The stream of each (grids, shapes) tile, in the order given. Every tile is coded as a stream of its own (the symbol layer's contexts start afresh at a tile), so coding them on several processes gives the SAME bytes as one process
    (tested); jobs 0 = automatic, 1 = this process only (the default: a script that calls this library at the top level, without `if __name__ == "__main__":`, cannot start worker processes; the converter's command line, which has the guard, asks for automatic). The tiles are handed over in windows of 16 per process so a very large picture never has all its tiles waiting in memory at once."""
    jobs = auto_jobs() if not jobs else jobs
    if jobs <= 1:
        for t in tiles: yield _encode_one(t)
        return
    import multiprocessing
    with multiprocessing.Pool(jobs) as pool:
        window = []
        for t in tiles:
            window.append(t)
            if len(window) >= jobs * 16: yield from pool.map(_encode_one, window, chunksize=4); window = []
        if window: yield from pool.map(_encode_one, window, chunksize=4)

def write_container(width, height, orientation, thumb565, levels, jobs=1):
    """levels: list of dicts: kind, pic (a jpegcoef.Jpeg-like coefficient picture; for KIND_FIT_RAW the RGB888 canvas bytes under 'raw'), rect (x, y, w, h placement in the 800 x 720 canvas for the fit levels, else (0, 0, w, h)).
    The level order in the file is the list order (put the fit level first: it is the first thing a viewer reads)."""
    assert len(thumb565) == THUMB_BYTES and 0 < len(levels) <= MAX_LEVELS
    tables = []; entries = []; blobs = []
    for lv in levels:
        e = dict(w=0, h=0, rect=lv["rect"], layout=LAYOUT_TILES, mode=MODE_TGY, pixfmt=0, kind=lv["kind"], qy=0, qc=0, tile_log2=7, flags=0, data_off=0, data_len=0, index_off=0, index_len=0, index_crc=0)
        if lv["kind"] == KIND_FIT_RAW:
            e.update(w=CANVAS_W, h=CANVAS_H, layout=LAYOUT_RASTER, mode=MODE_RAW); data = lv["raw"]; assert len(data) == CANVAS_W * CANVAS_H * 3; blobs.append((b"", data))
        else:
            pic = lv["pic"]; sub = sub_of(pic); e.update(w=pic.width, h=pic.height, pixfmt=sub)
            for slot, key in ((0, "qy"), (1, "qc")):
                if slot == 1 and sub == SUB_GREY: continue
                t = tuple(int(x) for x in pic.quant[slot]);
                if t not in tables: tables.append(t)
                e[key] = tables.index(t)
            assert len(tables) <= MAX_TABLES
            streams = []; index = b""; pos = 0
            for s in encode_tiles(((grids, shapes) for ty, tx, grids, shapes in R.split_tiles(pic)), jobs):
                streams.append(s); index += struct.pack("<III", pos, len(s), crc(s)); pos += len(s)
            blobs.append((index, b"".join(streams))); e["index_crc"] = crc(index)
        entries.append(e)
    tables_blob = struct.pack("<H", len(tables)) + b"".join(struct.pack("<64H", *t) for t in tables)
    lt_off = HEADER; tab_off = lt_off + LEVEL * len(levels); thumb_off = align(tab_off + len(tables_blob)); pos = thumb_off + align(THUMB_BYTES)
    for e, (index, data) in zip(entries, blobs):
        if index: e["index_off"], e["index_len"] = pos, len(index); pos = align(pos + len(index))
        e["data_off"], e["data_len"] = pos, len(data); pos = align(pos + len(data))
    size = pos; buf = bytearray(size)
    channels = 1 if any(lv.get("pic") is not None and len(lv["pic"].components) == 1 for lv in levels) else 3
    head = struct.pack("<4sHHIIBBBBIIIIHHIII", MAGIC, VERSION, HEADER, width, height, channels, 0, orientation, 0, len(levels), lt_off, thumb_off, THUMB_BYTES, 96, 72, tab_off, len(tables_blob), size)
    head += struct.pack("<I", crc(head)) + bytes(HEADER - len(head) - 4)
    assert len(head) == HEADER
    buf[:HEADER] = head
    for i, e in enumerate(entries): buf[lt_off + i * LEVEL:lt_off + (i + 1) * LEVEL] = pack_level(e)
    buf[tab_off:tab_off + len(tables_blob)] = tables_blob; buf[thumb_off:thumb_off + THUMB_BYTES] = thumb565
    for e, (index, data) in zip(entries, blobs):
        if index: buf[e["index_off"]:e["index_off"] + len(index)] = index
        buf[e["data_off"]:e["data_off"] + len(data)] = data
    return bytes(buf)

class Container:
    def __init__(self, data):
        self.data = data; n = len(data)
        if n < HEADER or data[:4] != MAGIC: raise ValueError("not a TGY file")
        (_, ver, hsz, w, h, ch, _z, orient, flags, nlev, lt_off, th_off, th_len, th_w, th_h, tab_off, tab_len, size) = struct.unpack_from("<4sHHIIBBBBIIIIHHIII", data, 0)
        if ver != VERSION or hsz != HEADER: raise ValueError("unknown TGY container version")
        if crc(data[:52]) != struct.unpack_from("<I", data, 52)[0]: raise ValueError("header checksum")
        if size != n: raise ValueError("file size differs from the header")
        if not (1 <= nlev <= MAX_LEVELS) or lt_off < HEADER or lt_off + nlev * LEVEL > n: raise ValueError("level table out of range")
        if tab_off < HEADER or tab_off + tab_len > n or tab_len < 2: raise ValueError("table section out of range")
        if th_len != THUMB_BYTES or th_off + th_len > n: raise ValueError("thumbnail out of range")
        self.width, self.height, self.channels, self.orientation, self.thumb = w, h, ch, orient, data[th_off:th_off + th_len]
        nt = struct.unpack_from("<H", data, tab_off)[0]
        if nt > MAX_TABLES or 2 + nt * 128 > tab_len: raise ValueError("quantisation tables out of range")
        self.tables = [list(struct.unpack_from("<64H", data, tab_off + 2 + 128 * i)) for i in range(nt)]
        for t in self.tables:
            if min(t) < 1: raise ValueError("quantisation table entry of zero")
        self.levels = []
        for i in range(nlev):
            f = struct.unpack_from("<IIHHHHBBBBBBBBIIIIII", data, lt_off + i * LEVEL)
            e = dict(w=f[0], h=f[1], rect=f[2:6], layout=f[6], mode=f[7], pixfmt=f[8], kind=f[9], qy=f[10], qc=f[11], tile_log2=f[12], flags=f[13], data_off=f[14], data_len=f[15], index_off=f[16], index_len=f[17], index_crc=f[18])
            if e["data_off"] + e["data_len"] > n or e["data_off"] < HEADER: raise ValueError("level data out of range")
            e["supported"] = (e["layout"], e["mode"]) in ((LAYOUT_RASTER, MODE_RAW), (LAYOUT_TILES, MODE_TGY)) and e["tile_log2"] == 7
            if e["supported"] and e["mode"] == MODE_TGY:
                if e["pixfmt"] not in (SUB_GREY, SUB_444, SUB_420) or e["w"] < 1 or e["h"] < 1 or e["w"] > 65535 or e["h"] > 65535: raise ValueError("bad tile level geometry")
                e["rows"], e["cols"] = tile_grid(e["w"], e["h"], e["pixfmt"])
                if e["index_len"] != e["rows"] * e["cols"] * TILE_INDEX or e["index_off"] + e["index_len"] > n: raise ValueError("tile index size or position wrong")
                if crc(data[e["index_off"]:e["index_off"] + e["index_len"]]) != e["index_crc"]: raise ValueError("tile index checksum")
                if e["qy"] >= nt or (e["pixfmt"] != SUB_GREY and e["qc"] >= nt): raise ValueError("quantisation table index out of range")
            elif e["supported"] and ((e["w"], e["h"]) != (CANVAS_W, CANVAS_H) or e["data_len"] != CANVAS_W * CANVAS_H * 3): raise ValueError("bad raw level")
            self.levels.append(e)
    def tile_stream(self, li, ty, tx):
        e = self.levels[li]
        if not e["supported"] or e["mode"] != MODE_TGY: raise ValueError("not a tile level")
        if not (0 <= ty < e["rows"] and 0 <= tx < e["cols"]): raise ValueError("tile out of range")
        off, ln, c = struct.unpack_from("<III", self.data, e["index_off"] + (ty * e["cols"] + tx) * TILE_INDEX)
        if off + ln > e["data_len"]: raise ValueError("tile stream out of range")
        s = self.data[e["data_off"] + off:e["data_off"] + off + ln]
        if crc(s) != c: raise ValueError("tile checksum")
        return s
    def picture(self, li):
        """The level's coefficient picture (every tile decoded, CRC checked)."""
        e = self.levels[li]; pic = blank_picture(e["w"], e["h"], e["pixfmt"], [self.tables[e["qy"]]] + ([self.tables[e["qc"]]] if e["pixfmt"] != SUB_GREY else []))
        shape_src = R.split_tiles(pic); out = []
        for ty, tx, _g, shapes in shape_src: out.append((ty, tx, V.decode_tile(self.tile_stream(li, ty, tx), shapes), shapes))
        return R.assemble(pic, out)
    def pixels(self, li, profile="tgy"):
        e = self.levels[li]
        if e["mode"] == MODE_RAW: return np.frombuffer(self.data[e["data_off"]:e["data_off"] + e["data_len"]], dtype=np.uint8).reshape(CANVAS_H, CANVAS_W, 3)
        pic = self.picture(li); return R.decode_pixels_tgy(pic) if profile == "tgy" else R.decode_pixels(pic)
