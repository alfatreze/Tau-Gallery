"""Tau Gallery container (TGY) v0 and folder index (TGIX) v0: the M1 formats (docs/M1_DESIGN.md).

A .tgy file holds ONE picture: a header, one thumbnail (96 x 72 RGB565) and a "fit canvas": the picture with its EXIF orientation applied, scaled to fit 800 x 720 and centred on a black canvas of exactly that size,
stored as the viewer plane wants it (rows of 800 pixels of R, G, B bytes, 2,400 bytes a row), so the whole fit view is ONE contiguous read that lands in the plane (docs/M0_DESIGN.md 3). The level table has room for
the later levels (tiles, TGY payloads: layout and mode fields); a reader must reject a level whose layout or mode it does not know and may skip levels it does not need.
Everything is little-endian. Sections start on 4 KB boundaries (APF reads are fastest 4 KB aligned)."""
import io, struct, zlib
from pathlib import Path

CANVAS_W, CANVAS_H = 800, 720
THUMB_W, THUMB_H = 96, 72
THUMB_BYTES = THUMB_W * THUMB_H * 2                     # 13,824
ALIGN = 4096
TGY_MAGIC, TGY_VERSION, TGY_HEADER = b"TGY\0", 0, 64
LEVEL_ENTRY = 48
IDX_MAGIC, IDX_VERSION, IDX_HEADER, IDX_ENTRY = b"TGIX", 0, 32, 160
LAYOUT_RASTER, MODE_RAW, PIX_RGB888, KIND_FIT = 0, 0, 0, 0
LAYOUT_TILE128, KIND_ZOOM = 1, 1                       # M2 (docs/M2_DESIGN.md section 6): a zoom level of raw 128 x 128 tiles, row-major, 49,152 bytes each; ids 2 (compressed, band-major) and 3 (macro-tiles) are reserved
TILE, TILE_BYTES, Q16 = 128, 128 * 128 * 3, 65536
RASTER_MAX_W = 2048                                      # a zoom stop up to this wide is stored as full-width row strips (option A, docs/M2_DESIGN.md section 3); wider ones as tiles (option B)
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

def prepare(img):
    """A source picture ready for every level: the EXIF orientation applied, converted to sRGB when it carries another ICC profile, plain RGB."""
    from PIL import ImageOps
    return to_srgb(ImageOps.exif_transpose(img)).convert("RGB")

def render(img, upscale=False):
    """(canvas RGB888 bytes of 800 x 720, content rect, thumbnail RGB565 bytes) from a PIL image; the orientation is applied here."""
    return render_prepared(prepare(img), upscale)

def render_prepared(img, upscale=False):
    """render() for an image that prepare() has already seen."""
    from PIL import Image
    x, y, rw, rh = fit_rect(img.width, img.height, upscale=upscale)
    small = img.resize((rw, rh), Image.LANCZOS) if (rw, rh) != img.size else img
    canvas = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0)); canvas.paste(small, (x, y))
    tx, ty, tw, th = fit_rect(img.width, img.height, THUMB_W, THUMB_H, upscale=False)
    tsmall = img.resize((tw, th), Image.LANCZOS)
    tc = Image.new("RGB", (THUMB_W, THUMB_H), (0, 0, 0)); tc.paste(tsmall, ((THUMB_W - tw) // 2, (THUMB_H - th) // 2))
    return canvas.tobytes(), (x, y, rw, rh), rgb565_le(tc.tobytes()), img.size

def ladder(sw, sh):
    """The zoom ladder of a source of sw x sh pixels (orientation applied), the same integer arithmetic as fw2/gallery/gallery_view.h gv_ladder: a list of (width, height, scale_q16), the fit canvas first.
    Fit is never enlarged. 1/2 only for a big source (1,600 wide or 1,440 tall) and only if it is at least 1.5x past fit; 1:1 only when the source is larger than the screen (800 x 720)."""
    fq = min(CANVAS_W * Q16 // sw, CANVAS_H * Q16 // sh, Q16)
    def stop(sc): return (max(1, (sw * sc + Q16 // 2) // Q16), max(1, (sh * sc + Q16 // 2) // Q16), sc)
    out = [stop(fq)]
    if sw > CANVAS_W or sh > CANVAS_H:
        if (sw >= 1600 or sh >= 1440) and (Q16 // 2) * 2 >= fq * 3: out.append(stop(Q16 // 2))
        out.append(stop(Q16))
    return out

def tile_data(img):
    """The raw tile level of a PIL RGB image: 128 x 128 tiles, row-major, each 49,152 bytes (R, G, B, rows of 128 pixels), the edge tiles padded with black. Returns (bytes, tiles across, tiles down)."""
    from PIL import Image
    ntx, nty = (img.width + TILE - 1) // TILE, (img.height + TILE - 1) // TILE
    out = bytearray()
    for ty in range(nty):
        for tx in range(ntx):
            out += img.crop((tx * TILE, ty * TILE, tx * TILE + TILE, ty * TILE + TILE)).tobytes()          # a crop past the image is black
    assert len(out) == ntx * nty * TILE_BYTES
    return bytes(out), ntx, nty

def to_srgb(img):
    """An image with an ICC profile other than sRGB (Display P3 is common from phones) converted to sRGB; the Pocket's panel and every viewer assume sRGB. A missing, unreadable or sRGB profile leaves the pixels alone."""
    icc = img.info.get("icc_profile")
    if not icc or img.mode not in ("RGB", "RGBA"): return img
    try:
        from PIL import ImageCms
        prof = ImageCms.ImageCmsProfile(io.BytesIO(icc))
        if "srgb" in ImageCms.getProfileDescription(prof).lower(): return img
        out = ImageCms.profileToProfile(img.convert("RGB"), prof, ImageCms.createProfile("sRGB"), outputMode="RGB")
        return out if out is not None else img
    except Exception:
        return img

def raster_level(img, lw, lh):
    """A zoom stop as full-width row strips (option A): the picture padded onto a canvas of at least 800 x 720 (centred, even offsets, black) so the viewer's plane line (1,200 words) is always whole, rows of
    ceil(width * 3 / 4) * 4 bytes. Returns a dict for pack_tgy: storage size w x h, the content rect, the row stride in bytes, the data."""
    from PIL import Image
    sw, sh = max(lw, CANVAS_W), max(lh, CANVAS_H)
    px, py = ((sw - lw) // 2) & ~1, (sh - lh) // 2
    lv = img if (lw, lh) == img.size else img.resize((lw, lh), Image.LANCZOS)
    canvas = Image.new("RGB", (sw, sh), (0, 0, 0)); canvas.paste(lv, (px, py))
    stride = (sw * 3 + 3) // 4 * 4
    rows = canvas.tobytes(); pad = bytes(stride - sw * 3)
    data = b"".join(rows[r * sw * 3:(r + 1) * sw * 3] + pad for r in range(sh))
    return dict(raster=True, w=sw, h=sh, rect=(px, py, lw, lh), stride=stride, data=data)

def zoom_levels(img):
    """The zoom levels of a prepared image for the ladder stops past fit. A stop up to RASTER_MAX_W wide is a raster level (option A, a dict), a wider one a list entry (width, height, tile bytes) (option B).
    Each stop is resampled from the full-size picture (never from the fit canvas); the 1:1 stop is the picture itself."""
    from PIL import Image
    out = []
    for (lw, lh, sc) in ladder(img.width, img.height)[1:]:
        if lw <= RASTER_MAX_W: out.append(raster_level(img, lw, lh)); continue
        lv = img if (lw, lh) == img.size else img.resize((lw, lh), Image.LANCZOS)
        data, ntx, nty = tile_data(lv); out.append((lw, lh, data))
    return out

def pack_tgy(canvas, rect, thumb, size, levels=()):
    """One .tgy file. `levels` (M2, optional) are zoom levels after the fit canvas, 4 KB aligned, one level table entry each: a dict from raster_level (layout 0, kind 1: full-width row strips on a canvas of at least
    800 x 720, `stride` bytes a row, `rect` the content) or a tuple (width, height, tile bytes from tile_data) (layout 1, kind 1, stride 384 = a tile row, no index: a tile's offset is the level's offset plus
    (row * tiles across + column) * 49,152). Without levels the file is byte for byte the M1 file."""
    w, h = size
    assert len(canvas) == CANVAS_W * CANVAS_H * 3 and len(thumb) == THUMB_BYTES
    nlev = 1 + len(levels); assert TGY_HEADER + nlev * LEVEL_ENTRY <= ALIGN
    thumb_off = ALIGN; data_off = thumb_off + align(THUMB_BYTES)
    data_len = len(canvas); end = data_off + align(data_len)
    entries = [struct.pack("<IIHHHHBBBBIIIIIII", CANVAS_W, CANVAS_H, *rect, LAYOUT_RASTER, MODE_RAW, PIX_RGB888, KIND_FIT,
                           CANVAS_W * 3, data_off, data_len, 0, 0, crc(canvas), 0)]
    placed = []
    for lvl in levels:
        if isinstance(lvl, dict):                                   # a raster zoom level (option A)
            data = lvl["data"]; assert len(data) == lvl["h"] * lvl["stride"] and lvl["stride"] == (lvl["w"] * 3 + 3) // 4 * 4
            entries.append(struct.pack("<IIHHHHBBBBIIIIIII", lvl["w"], lvl["h"], *lvl["rect"], LAYOUT_RASTER, MODE_RAW, PIX_RGB888, KIND_ZOOM, lvl["stride"], end, len(data), 0, 0, crc(data), 0))
        else:                                                       # a tile level (option B)
            lw, lh, data = lvl; ntx, nty = (lw + TILE - 1) // TILE, (lh + TILE - 1) // TILE
            assert len(data) == ntx * nty * TILE_BYTES
            entries.append(struct.pack("<IIHHHHBBBBIIIIIII", lw, lh, 0, 0, lw, lh, LAYOUT_TILE128, MODE_RAW, PIX_RGB888, KIND_ZOOM, TILE * 3, end, len(data), 0, 0, crc(data), 0))
        placed.append((end, data)); end += align(len(data))
    file_size = end
    for e in entries: assert len(e) == LEVEL_ENTRY
    head = struct.pack("<4sHHIIBBBBIIIIHHIII", TGY_MAGIC, TGY_VERSION, TGY_HEADER, w, h, 3, 7, 0, 0, nlev, TGY_HEADER, thumb_off, THUMB_BYTES, THUMB_W, THUMB_H, 0, 0, file_size)
    head += struct.pack("<I", crc(head)) + bytes(TGY_HEADER - len(head) - 4)
    assert len(head) == TGY_HEADER
    buf = bytearray(file_size)
    buf[0:TGY_HEADER] = head
    for i, e in enumerate(entries): buf[TGY_HEADER + i * LEVEL_ENTRY:TGY_HEADER + (i + 1) * LEVEL_ENTRY] = e
    buf[thumb_off:thumb_off + THUMB_BYTES] = thumb; buf[data_off:data_off + data_len] = canvas
    for off, data in placed: buf[off:off + len(data)] = data
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
    """The bytes of a raster fit canvas or of a raw tile level, checked against the level's CRC."""
    known = (lv["layout"] == LAYOUT_RASTER or lv["layout"] == LAYOUT_TILE128) and lv["mode"] == MODE_RAW and lv["pixfmt"] == PIX_RGB888
    if not known: raise ValueError("level layout or mode not known")
    b = data[lv["off"]:lv["off"] + lv["len"]]
    if crc(b) != lv["crc"]: raise ValueError("level checksum")
    return b

def row_of(data, lv, row):
    """The stride bytes of row `row` of a raster zoom level (storage rows, padding included)."""
    if lv["layout"] != LAYOUT_RASTER or lv["kind"] != KIND_ZOOM: raise ValueError("not a raster zoom level")
    o = lv["off"] + row * lv["stride"]; return data[o:o + lv["stride"]]

def tile_of(data, lv, tx, ty):
    """The 49,152 bytes of tile (tx, ty) of a raw tile level."""
    ntx = (lv["w"] + TILE - 1) // TILE
    if lv["layout"] != LAYOUT_TILE128: raise ValueError("not a tile level")
    o = lv["off"] + (ty * ntx + tx) * TILE_BYTES
    return data[o:o + TILE_BYTES]

def pack_index(entries, thumbs):
    """entries: list of dict(path, name, size, width, height); thumbs: list of THUMB_BYTES. The atlas follows the entries, one thumbnail per entry, in entry order."""
    n = len(entries); ent_off = IDX_HEADER; atlas_off = align(ent_off + n * IDX_ENTRY)
    head = struct.pack("<4sHHIIIIHHI", IDX_MAGIC, IDX_VERSION, IDX_HEADER, n, IDX_ENTRY, ent_off, atlas_off, THUMB_W, THUMB_H, 0)
    assert len(head) == IDX_HEADER
    buf = bytearray(align(atlas_off + n * THUMB_BYTES)); buf[:IDX_HEADER] = head                  # padded: a 4 KB read of the last piece must not run past the end of the file
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
