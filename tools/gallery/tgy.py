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
RASTER_MAX_BYTES = 9437184                               # a zoom stop whose padded canvas (at least 800 x 720) is at most 9 MiB is stored as full-width row strips (option A, docs/M2_DESIGN.md section 3: the zoom plane holds 9 MiB); a bigger one as tiles (option B), if both its sides are at least the screen's; else it is left out
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

def _icc_profile(img):
    """The ImageCms profile of an RGB image whose ICC profile is readable and is not sRGB, else None (the case to_srgb leaves alone)."""
    icc = img.info.get("icc_profile")
    if not icc or img.mode not in ("RGB", "RGBA"): return None
    try:
        from PIL import ImageCms
        prof = ImageCms.ImageCmsProfile(io.BytesIO(icc))
        return None if "srgb" in ImageCms.getProfileDescription(prof).lower() else prof
    except Exception:
        return None

def needs_srgb(img):
    """True when to_srgb will change this image's colours (a readable ICC profile that is not sRGB). A zoom level that keeps a JPEG's own coefficients cannot be converted, so such a picture's zoom level is encoded again from the converted picture."""
    return _icc_profile(img) is not None

def to_srgb(img):
    """An image with an ICC profile other than sRGB (Display P3 is common from phones) converted to sRGB; the Pocket's panel and every viewer assume sRGB. A missing, unreadable or sRGB profile leaves the pixels alone."""
    prof = _icc_profile(img)
    if prof is None: return img
    try:
        from PIL import ImageCms
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
    """The zoom levels of a prepared image for the ladder stops past fit. A stop whose padded canvas (at least 800 x 720) fits RASTER_MAX_BYTES is a raster level (option A, a dict); a bigger one is a tile level
    (option B, a tuple (width, height, tile bytes)) when both its sides are at least the screen's, else it is left out (a window cannot show an axis shorter than the screen). Each stop is resampled from the full-size
    picture (never from the fit canvas); the 1:1 stop is the picture itself."""
    from PIL import Image
    out = []
    for (lw, lh, sc) in ladder(img.width, img.height)[1:]:
        if max(lw, CANVAS_W) * max(lh, CANVAS_H) * 3 <= RASTER_MAX_BYTES: out.append(raster_level(img, lw, lh)); continue
        if lw < CANVAS_W or lh < CANVAS_H: continue
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
    """The entries of an index of either version (dicts with path, name, size, width, height, thumb; a version 1 entry also has folder, taken and details). A version 1 thumbnail is 96 x 96, a version 0 one 96 x 72."""
    if len(data) >= 6 and data[:4] == IDX_MAGIC and struct.unpack_from("<H", data, 4)[0] == 1: return parse_index_full(data)["entries"]
    return parse_index_v0(data)

def parse_index_v0(data):
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

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
# TGIX version 1 (docs/features/TGIX_V1_SPEC.md): the library index of Tau Gallery 1.0: a folder table (one level), per-picture details, 96 x 96 square thumbnails, a sort key. The reader of the firmware is
# fw2/gallery/gallery_fmt.c; this is the reference writer and reader. Layout: header (64) | entries (n x 176) | pad 4 KB | atlas (n x 18,432) | pad | folders (f x 48) | details | pad 4 KB.
IDX1_VERSION, IDX1_HEADER, IDX1_ENTRY, FOLDER_ENTRY, FOLDER_NAME = 1, 64, 176, 48, 32
THUMB1_W = THUMB1_H = 96
THUMB1_BYTES = THUMB1_W * THUMB1_H * 2                  # 18,432
MAX_ENTRIES, MAX_FOLDERS, MAX_DETAILS = 65535, 255, 255
SORT_NAME, SORT_TAKEN, SORT_CONVERTED = 0, 1, 2
(DT_FORMAT, DT_CAMERA, DT_LENS, DT_TAKEN, DT_SHUTTER, DT_APERTURE, DT_ISO, DT_FOCAL) = range(1, 9)
ALL_PICTURES = "ALL PICTURES"

def square_thumb(img):
    """The picture scaled to cover a 96 x 96 square and cropped to it (a centre crop), RGB565 little-endian, 18,432 bytes."""
    from PIL import Image
    s = max(THUMB1_W / img.width, THUMB1_H / img.height)
    w, h = max(THUMB1_W, round(img.width * s)), max(THUMB1_H, round(img.height * s))
    small = img.resize((w, h), Image.LANCZOS) if (w, h) != img.size else img
    x, y = (w - THUMB1_W) // 2, (h - THUMB1_H) // 2
    return rgb565_le(small.crop((x, y, x + THUMB1_W, y + THUMB1_H)).convert("RGB").tobytes())

def thumb72_to_square(t72):
    """A version 0 thumbnail (96 x 72) padded to 96 x 96 with black bars (12 rows above and below), for merging an old index."""
    assert len(t72) == THUMB_BYTES
    return bytes(12 * THUMB1_W * 2) + t72 + bytes(12 * THUMB1_W * 2)

def pack_details(d):
    """Details items (`tag u8, length u8, bytes`) for the fields present in `d`; a field that is missing or None is not stored. Keys: format (ASCII, 15), camera, lens (Latin-1, 40), taken (year, month, day, hour, minute, second),
    shutter (numerator, denominator), aperture (f-number x 10), iso, focal (millimetres x 10)."""
    out = bytearray()
    def item(tag, b): out.extend(bytes((tag, len(b))) + b)
    if d.get("format"): item(DT_FORMAT, d["format"].encode("ascii", "replace")[:15])
    if d.get("camera"): item(DT_CAMERA, d["camera"].encode("latin-1", "replace")[:40])
    if d.get("lens"): item(DT_LENS, d["lens"].encode("latin-1", "replace")[:40])
    if d.get("taken"): y, mo, dd, h, mi, s = d["taken"]; item(DT_TAKEN, struct.pack("<HBBBBB", y, mo, dd, h, mi, s))
    if d.get("shutter"): n, de = d["shutter"]; item(DT_SHUTTER, struct.pack("<II", n, de))
    if d.get("aperture"): item(DT_APERTURE, struct.pack("<H", d["aperture"]))
    if d.get("iso"): item(DT_ISO, struct.pack("<I", d["iso"]))
    if d.get("focal"): item(DT_FOCAL, struct.pack("<H", d["focal"]))
    assert len(out) <= MAX_DETAILS
    return bytes(out)

def parse_details(b):
    """The items of a details run as a dict (the same keys as pack_details); an unknown tag is skipped, a run that overruns raises."""
    d = {}; i = 0
    while i < len(b):
        if i + 2 > len(b) or i + 2 + b[i + 1] > len(b): raise ValueError("a details run overruns")
        tag, n = b[i], b[i + 1]; v = b[i + 2:i + 2 + n]; i += 2 + n
        if tag == DT_FORMAT: d["format"] = v.decode("ascii", "replace")
        elif tag == DT_CAMERA: d["camera"] = v.decode("latin-1")
        elif tag == DT_LENS: d["lens"] = v.decode("latin-1")
        elif tag == DT_TAKEN and n == 7: d["taken"] = struct.unpack("<HBBBBB", v)
        elif tag == DT_SHUTTER and n == 8: d["shutter"] = struct.unpack("<II", v)
        elif tag == DT_APERTURE and n == 2: d["aperture"] = struct.unpack("<H", v)[0]
        elif tag == DT_ISO and n == 4: d["iso"] = struct.unpack("<I", v)[0]
        elif tag == DT_FOCAL and n == 2: d["focal"] = struct.unpack("<H", v)[0]
    return d

def exif_details(img, source_format=None):
    """The details a picture has, read from its EXIF (Pillow): a dict for pack_details. GPS is never read."""
    from fractions import Fraction
    d = {}
    if source_format: d["format"] = source_format
    try: ex = img.getexif(); ifd = ex.get_ifd(0x8769)
    except Exception: return d
    mk, md = str(ex.get(271, "") or "").strip("\0 "), str(ex.get(272, "") or "").strip("\0 ")
    if md: d["camera"] = md if (not mk or md.lower().startswith(mk.lower())) else mk + " " + md
    elif mk: d["camera"] = mk
    lens = str(ifd.get(0xA434, "") or "").strip("\0 ")
    if lens: d["lens"] = lens
    ts = str(ifd.get(0x9003, "") or ex.get(306, "") or "").strip("\0 ")
    try:
        dt, tm = ts.split(" "); y, mo, dd = (int(x) for x in dt.split(":")); h, mi, s = (int(x) for x in tm.split(":"))
        if 1990 <= y <= 2100 and 1 <= mo <= 12 and 1 <= dd <= 31 and h < 24 and mi < 60 and s < 61: d["taken"] = (y, mo, dd, h, mi, min(s, 59))
    except Exception: pass
    try:
        et = float(ifd.get(0x829A)); fr = Fraction(et).limit_denominator(100000)
        if et > 0 and fr.numerator > 0: d["shutter"] = (fr.numerator, fr.denominator)
    except Exception: pass
    try:
        fn = float(ifd.get(0x829D))
        if 0 < fn < 6553: d["aperture"] = int(round(fn * 10))
    except Exception: pass
    try:
        iso = ifd.get(0x8827); iso = iso[0] if isinstance(iso, (tuple, list)) else iso
        if iso and int(iso) > 0: d["iso"] = int(iso)
    except Exception: pass
    try:
        fl = float(ifd.get(0x920A))
        if 0 < fl < 6553: d["focal"] = int(round(fl * 10))
    except Exception: pass
    return d

def taken_seconds(taken):
    """Seconds since 2000-01-01 00:00:00 of a `taken` tuple (the clock as written, no time zone), 0 when unknown."""
    import datetime
    if not taken: return 0
    try: s = int((datetime.datetime(*taken) - datetime.datetime(2000, 1, 1)).total_seconds())
    except ValueError: return 0
    return s if s > 0 else 0

def pack_index_v1(entries, thumbs, folders=None, sort=SORT_NAME):
    """entries: dict(path, name, size, width, height, folder=0, taken=0, details=b'' or a dict for pack_details), sorted by folder (the range of a folder is [first, first + count)); thumbs: THUMB1_BYTES each, in entry order;
    folders: dict(name, cover=None) per folder (default one folder, ALL PICTURES). The file is padded to 4 KB at the end."""
    n = len(entries)
    folders = folders or [dict(name=ALL_PICTURES)]
    if n > MAX_ENTRIES or len(thumbs) != n: raise ValueError("entry count")
    if not 1 <= len(folders) <= MAX_FOLDERS: raise ValueError("folder count")
    fi = [e.get("folder", 0) for e in entries]
    if any(not 0 <= f < len(folders) for f in fi) or fi != sorted(fi): raise ValueError("entries must be sorted by folder, each folder index in range")
    nfo = len(folders)
    par = [f.get("parent") for f in folders]                                                                 # the folder tree (TGIX 1.1): a folder's parent index (None: top level); the table is in depth first order, so a folder's descendants follow it
    if any(p is not None and not 0 <= p < k for k, p in enumerate(par)): raise ValueError("a folder's parent must come before it")
    nfold = [0] * nfo
    for k in range(nfo):
        a = par[k]
        while a is not None: nfold[a] += 1; a = par[a]
    for k in range(nfo):                                                                                      # descendants are exactly the next nfold folders (depth first order)
        for j in range(k + 1, k + 1 + nfold[k]):
            a = par[j] if j < nfo else None
            while a is not None and a != k: a = par[a]
            if a != k: raise ValueError("folders are not in depth first order")
    own = [fi.count(k) for k in range(nfo)]
    if any(nfold[k] and own[k] for k in range(nfo)): raise ValueError("a folder that holds folders cannot hold pictures: put them in a folder of their own")
    if n and any(not nfold[k] and not own[k] for k in range(nfo)): raise ValueError("a folder with no picture")
    first = [fi.index(k) if k in fi else -1 for k in range(nfo)]; count = list(own)
    for k in range(nfo - 1, -1, -1):                                                                          # a folder that holds folders shows all their pictures: its range is theirs
        if nfold[k]: first[k] = min(first[j] for j in range(k + 1, k + 1 + nfold[k]) if first[j] >= 0) if n else 0; count[k] = sum(own[j] for j in range(k + 1, k + 1 + nfold[k]))
    det = bytearray(); recs = []
    for i, e in enumerate(entries):
        dd = e.get("details", b""); dd = pack_details(dd) if isinstance(dd, dict) else bytes(dd)
        if len(dd) > MAX_DETAILS: raise ValueError("details too long")
        recs.append((len(det), len(dd))); det.extend(dd)
    ent_off = IDX1_HEADER; atlas_off = align(ent_off + n * IDX1_ENTRY); fold_off = align(atlas_off + n * THUMB1_BYTES)
    det_off = fold_off + len(folders) * FOLDER_ENTRY; total = align(det_off + len(det) + 1)
    buf = bytearray(total)
    head = struct.pack("<4sHHIHHHHIIIHHIIIII", b"TGIX", IDX1_VERSION, IDX1_HEADER, n, IDX1_ENTRY, THUMB1_W, THUMB1_H, len(folders), ent_off, atlas_off, fold_off, FOLDER_ENTRY, sort, det_off, len(det), 0, 0, 0)
    head = head[:48] + bytes(12); buf[:60] = head[:60]; buf[60:64] = struct.pack("<I", crc(bytes(buf[:60])))
    for i, (e, t) in enumerate(zip(entries, thumbs)):
        p = e["path"].encode("utf-8"); nm = e["name"].encode("latin-1", "replace")
        if len(p) >= PATH_BYTES: raise ValueError("path too long for the index: " + e["path"])
        if len(t) != THUMB1_BYTES: raise ValueError("thumbnail size")
        do, dl = recs[i]
        rec = (p.ljust(PATH_BYTES, b"\0") + nm[:NAME_BYTES - 1].ljust(NAME_BYTES, b"\0") + struct.pack("<IIIIHHIHHI", e["size"], e["width"], e["height"], i, e.get("folder", 0), 1 if dl else 0, do, dl, 0, e.get("taken", 0)) + bytes(8))
        assert len(rec) == IDX1_ENTRY
        buf[ent_off + i * IDX1_ENTRY:ent_off + (i + 1) * IDX1_ENTRY] = rec
        buf[atlas_off + i * THUMB1_BYTES:atlas_off + (i + 1) * THUMB1_BYTES] = t
    for k, f in enumerate(folders):
        cover = f.get("cover"); cover = (first[k] if first[k] >= 0 else 0) if cover is None else cover
        rec = f["name"].encode("latin-1", "replace")[:FOLDER_NAME - 1].ljust(FOLDER_NAME, b"\0") + struct.pack("<IIIHH", max(first[k], 0), count[k], cover, 0 if par[k] is None else par[k] + 1, nfold[k])
        buf[fold_off + k * FOLDER_ENTRY:fold_off + (k + 1) * FOLDER_ENTRY] = rec
    buf[det_off:det_off + len(det)] = det
    return bytes(buf)

def parse_index_full(data):
    """Both versions: dict(version, sort, folders=[dict(name, first, count, cover)], entries=[dict(path, name, size, width, height, thumb (bytes), folder, taken, details (dict)), thumb_w, thumb_h]). Raises ValueError for
    anything the firmware's reader would refuse."""
    if len(data) < 32 or data[:4] != b"TGIX": raise ValueError("not a TGIX file")
    ver = struct.unpack_from("<H", data, 4)[0]
    if ver == 0:
        ents = parse_index(data)
        for e in ents: e.update(folder=0, taken=0, details={})
        return dict(version=0, sort=SORT_NAME, folders=[dict(name=ALL_PICTURES, first=0, count=len(ents), cover=0)], entries=ents, thumb_w=THUMB_W, thumb_h=THUMB_H)
    if ver != IDX1_VERSION or len(data) < IDX1_HEADER: raise ValueError("unknown TGIX version")
    (_, _, hsz, n, esz, tw, th, nf, eoff, aoff, foff, fesz, sort, doff, dlen, flags) = struct.unpack_from("<4sHHIHHHHIIIHHIII", data, 0)
    if crc(data[:60]) != struct.unpack_from("<I", data, 60)[0]: raise ValueError("header checksum")
    if hsz != IDX1_HEADER or esz != IDX1_ENTRY or fesz != FOLDER_ENTRY or (tw, th) != (THUMB1_W, THUMB1_H) or not 1 <= nf <= MAX_FOLDERS or n > MAX_ENTRIES or sort > 2: raise ValueError("TGIX header fields")
    if eoff < IDX1_HEADER or aoff < eoff + n * esz or foff < aoff + n * THUMB1_BYTES or doff < foff + nf * fesz or doff + dlen > len(data): raise ValueError("TGIX sections")
    folders = []
    for k in range(nf):
        r = data[foff + k * fesz:foff + (k + 1) * fesz]; first, count, cover, par1, nfold = struct.unpack_from("<IIIHH", r, FOLDER_NAME)
        if (count < 1 and n) or first + count > n or cover >= max(n, 1): raise ValueError("folder range")   # an empty library has one empty folder
        if par1 > k or k + nfold >= nf: par1 = nfold = 0                                                   # a tree field that cannot be right is ignored (the firmware does the same): a flat folder
        folders.append(dict(name=r[:FOLDER_NAME].split(b"\0")[0].decode("latin-1"), first=first, count=count, cover=cover, parent1=par1, nfold=nfold))
    ents = []
    for i in range(n):
        r = data[eoff + i * esz:eoff + (i + 1) * esz]
        size, w, h, ti, fo, fl, do, dl, _, taken = struct.unpack_from("<IIIIHHIHHI", r, PATH_BYTES + NAME_BYTES)
        if ti >= n or fo >= nf or do + dl > dlen: raise ValueError("entry fields")
        ents.append(dict(path=r[:PATH_BYTES].split(b"\0")[0].decode("utf-8"), name=r[PATH_BYTES:PATH_BYTES + NAME_BYTES].split(b"\0")[0].decode("latin-1"), size=size, width=w, height=h, folder=fo, taken=taken,
                         thumb=data[aoff + ti * THUMB1_BYTES:aoff + (ti + 1) * THUMB1_BYTES], details=parse_details(data[doff + do:doff + do + dl]) if dl else {}))
    for k, f in enumerate(folders):
        if f["nfold"] == 0 and any(ents[i]["folder"] != k for i in range(f["first"], f["first"] + f["count"])): raise ValueError("folder entries not contiguous")
        if f["nfold"] and (f["first"] != folders[k + 1]["first"] or f["count"] != sum(folders[j]["count"] for j in range(k + 1, k + 1 + f["nfold"]) if folders[j]["nfold"] == 0)): raise ValueError("folder tree ranges")
    return dict(version=1, sort=sort, folders=folders, entries=ents, thumb_w=THUMB1_W, thumb_h=THUMB1_H)
