"""Tau Gallery container (TGY) v0 and folder index (TGIX) v0: the M1 formats (docs/M1_DESIGN.md).

A .tgy file holds ONE picture: a header, one thumbnail (96 x 72 RGB565) and a "fit canvas": the picture with its EXIF orientation applied, scaled to fit 800 x 720 and centred on a black canvas of exactly that size,
stored as the viewer plane wants it (rows of 800 pixels of R, G, B bytes, 2,400 bytes a row), so the whole fit view is ONE contiguous read that lands in the plane (docs/M0_DESIGN.md 3). The level table has room for
the later levels (tiles, TGY payloads: layout and mode fields); a reader must reject a level whose layout or mode it does not know and may skip levels it does not need.
Everything is little-endian. Sections start on 4 KB boundaries (APF reads are fastest 4 KB aligned)."""
import struct, zlib
from pathlib import Path

CANVAS_W, CANVAS_H = 800, 720
THUMB_W, THUMB_H = 96, 72
THUMB_BYTES = THUMB_W * THUMB_H * 2                     # 13,824
ALIGN = 4096
TGY_MAGIC, TGY_VERSION, TGY_HEADER = b"TGY\0", 0, 64
LEVEL_ENTRY = 48
IDX_MAGIC, IDX_VERSION, IDX_HEADER, IDX_ENTRY = b"TGIX", 0, 32, 160
LAYOUT_RASTER, MODE_RAW, PIX_RGB888, KIND_FIT = 0, 0, 0, 0
PATH_BYTES, NAME_BYTES = 96, 40

def align(n, a=ALIGN): return (n + a - 1) // a * a
def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def fit_rect(w, h, cw=CANVAS_W, ch=CANVAS_H, upscale=False):
    """The picture's rectangle inside the canvas: scaled to fit (never enlarged unless upscale), centred; even x, so 4-pixel groups of the plane stay whole."""
    s = min(cw / w, ch / h)
    if not upscale: s = min(s, 1.0)
    rw, rh = max(1, round(w * s)), max(1, round(h * s))
    rw, rh = min(rw, cw), min(rh, ch)
    return (cw - rw) // 2 // 2 * 2, (ch - rh) // 2, rw, rh

def rgb565_le(rgb_bytes):
    """RGB888 bytes (R, G, B, ...) to little-endian RGB565 (truncating, the same reduction the Pocket's handheld display applies)."""
    out = bytearray(len(rgb_bytes) // 3 * 2)
    j = 0
    for i in range(0, len(rgb_bytes), 3):
        v = ((rgb_bytes[i] & 0xF8) << 8) | ((rgb_bytes[i + 1] & 0xFC) << 3) | (rgb_bytes[i + 2] >> 3)
        out[j] = v & 255; out[j + 1] = v >> 8; j += 2
    return bytes(out)

def render(img, upscale=False):
    """(canvas RGB888 bytes of 800 x 720, content rect, thumbnail RGB565 bytes) from a PIL image; the orientation is applied here."""
    from PIL import Image, ImageOps
    img = ImageOps.exif_transpose(img).convert("RGB")
    x, y, rw, rh = fit_rect(img.width, img.height, upscale=upscale)
    small = img.resize((rw, rh), Image.LANCZOS) if (rw, rh) != img.size else img
    canvas = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0)); canvas.paste(small, (x, y))
    tx, ty, tw, th = fit_rect(img.width, img.height, THUMB_W, THUMB_H, upscale=False)
    tsmall = img.resize((tw, th), Image.LANCZOS)
    tc = Image.new("RGB", (THUMB_W, THUMB_H), (0, 0, 0)); tc.paste(tsmall, ((THUMB_W - tw) // 2, (THUMB_H - th) // 2))
    return canvas.tobytes(), (x, y, rw, rh), rgb565_le(tc.tobytes()), img.size

def pack_tgy(canvas, rect, thumb, size):
    w, h = size
    assert len(canvas) == CANVAS_W * CANVAS_H * 3 and len(thumb) == THUMB_BYTES
    thumb_off = ALIGN; data_off = thumb_off + align(THUMB_BYTES)
    data_len = len(canvas); file_size = data_off + align(data_len)
    level = struct.pack("<IIHHHHBBBBIIIIIII", CANVAS_W, CANVAS_H, *rect, LAYOUT_RASTER, MODE_RAW, PIX_RGB888, KIND_FIT,
                        CANVAS_W * 3, data_off, data_len, 0, 0, crc(canvas), 0)
    assert len(level) == LEVEL_ENTRY
    head = struct.pack("<4sHHIIBBBBIIIIHHIII", TGY_MAGIC, TGY_VERSION, TGY_HEADER, w, h, 3, 7, 0, 0, 1, TGY_HEADER, thumb_off, THUMB_BYTES, THUMB_W, THUMB_H, 0, 0, file_size)
    head += struct.pack("<I", crc(head)) + bytes(TGY_HEADER - len(head) - 4)
    assert len(head) == TGY_HEADER
    buf = bytearray(file_size)
    buf[0:TGY_HEADER] = head; buf[TGY_HEADER:TGY_HEADER + LEVEL_ENTRY] = level
    buf[thumb_off:thumb_off + THUMB_BYTES] = thumb; buf[data_off:data_off + data_len] = canvas
    return bytes(buf)

def parse_tgy(data):
    """Header, thumbnail and levels of a .tgy (raises ValueError on anything it does not know). Returns a dict."""
    if len(data) < TGY_HEADER or data[:4] != TGY_MAGIC: raise ValueError("not a TGY file")
    (_, ver, hsz, w, h, ch, tl2, orient, flags, nlev, lt_off, th_off, th_len, th_w, th_h, meta_off, meta_len, fsize) = struct.unpack_from("<4sHHIIBBBBIIIIHHIII", data, 0)
    if ver != TGY_VERSION or hsz != TGY_HEADER: raise ValueError("unknown TGY version")
    if crc(data[:52]) != struct.unpack_from("<I", data, 52)[0]: raise ValueError("header checksum")
    if fsize != len(data): raise ValueError("file size differs from the header")
    levels = []
    for i in range(nlev):
        e = struct.unpack_from("<IIHHHHBBBBIIIIIII", data, lt_off + i * LEVEL_ENTRY)
        lv = dict(w=e[0], h=e[1], rect=e[2:6], layout=e[6], mode=e[7], pixfmt=e[8], kind=e[9], stride=e[10], off=e[11], len=e[12], index_off=e[13], index_len=e[14], crc=e[15])
        levels.append(lv)
    return dict(width=w, height=h, channels=ch, orientation=orient, levels=levels, thumb=(th_off, th_len, th_w, th_h), size=fsize)

def level_bytes(data, lv):
    if lv["layout"] != LAYOUT_RASTER or lv["mode"] != MODE_RAW or lv["pixfmt"] != PIX_RGB888: raise ValueError("level layout or mode not known")
    b = data[lv["off"]:lv["off"] + lv["len"]]
    if crc(b) != lv["crc"]: raise ValueError("level checksum")
    return b

def pack_index(entries, thumbs):
    """entries: list of dict(path, name, size, width, height); thumbs: list of THUMB_BYTES. The atlas follows the entries, one thumbnail per entry, in entry order."""
    n = len(entries); ent_off = IDX_HEADER; atlas_off = align(ent_off + n * IDX_ENTRY)
    head = struct.pack("<4sHHIIIIHHI", IDX_MAGIC, IDX_VERSION, IDX_HEADER, n, IDX_ENTRY, ent_off, atlas_off, THUMB_W, THUMB_H, 0)
    assert len(head) == IDX_HEADER
    buf = bytearray(atlas_off + n * THUMB_BYTES); buf[:IDX_HEADER] = head
    for i, (e, t) in enumerate(zip(entries, thumbs)):
        p = e["path"].encode("utf-8"); nm = e["name"].encode("latin-1", "replace")
        if len(p) >= PATH_BYTES: raise ValueError("path too long for the index: " + e["path"])
        rec = p.ljust(PATH_BYTES, b"\0") + nm[:NAME_BYTES - 1].ljust(NAME_BYTES, b"\0") + struct.pack("<IIII", e["size"], e["width"], e["height"], i) + bytes(IDX_ENTRY - PATH_BYTES - NAME_BYTES - 16)
        assert len(rec) == IDX_ENTRY
        buf[ent_off + i * IDX_ENTRY:ent_off + (i + 1) * IDX_ENTRY] = rec
        buf[atlas_off + i * THUMB_BYTES:atlas_off + (i + 1) * THUMB_BYTES] = t
    return bytes(buf)

def parse_index(data):
    if len(data) < IDX_HEADER or data[:4] != IDX_MAGIC: raise ValueError("not a TGIX file")
    (_, ver, hsz, n, esz, eoff, aoff, tw, th, _) = struct.unpack_from("<4sHHIIIIHHI", data, 0)
    if ver != IDX_VERSION or hsz != IDX_HEADER or esz != IDX_ENTRY or (tw, th) != (THUMB_W, THUMB_H): raise ValueError("unknown TGIX version")
    out = []
    for i in range(n):
        r = data[eoff + i * esz:eoff + (i + 1) * esz]
        path = r[:PATH_BYTES].split(b"\0")[0].decode("utf-8"); name = r[PATH_BYTES:PATH_BYTES + NAME_BYTES].split(b"\0")[0].decode("latin-1")
        size, w, h, ti = struct.unpack_from("<IIII", r, PATH_BYTES + NAME_BYTES)
        out.append(dict(path=path, name=name, size=size, width=w, height=h, thumb=data[aoff + ti * THUMB_BYTES:aoff + (ti + 1) * THUMB_BYTES]))
    return out
