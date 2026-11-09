#!/usr/bin/env python3
"""M2.1 formats (docs/M2_DESIGN.md section 6): the zoom ladder, the raw tile levels in a .tgy, the C reader of them, the converter's --levels option and its sRGB conversion.
1. tgy.ladder (Python) equals gv_ladder (the firmware's header) on fixed and random sizes. 2. tile_data cuts a picture into 128 x 128 tiles, row-major, edges black. 3. A .tgy with levels is the M1 file plus levels: level 0 is
untouched, offsets are 4 KB aligned, checksums hold, a tile is where the formula says. 4. The C reader (gt_zoom_level, gt_tile_off) agrees with the Python parse and refuses damaged levels. 5. The M1 reader (gt_parse) still finds
the fit canvas in a file with levels. 6. tgy_convert --levels writes the right levels for the right pictures and none by default. 7. A Display P3 picture is converted to sRGB; sRGB, no profile and a broken profile are left alone.
`--mutants` edits copies of tgy.py and gallery_fmt.c; every edit must make this test fail."""
import io, json, os, random, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
from PIL import Image
fails = 0
def check(c, what, extra=""):
    global fails
    print(("ok   " if c else "FAIL ") + what + ("" if c else "  " + str(extra)[:300]))
    if not c: fails += 1

def sdk_env():
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    return env

LADDER_C = r'''#include <stdio.h>
#include <stdlib.h>
#include "gallery_view.h"
int main(int argc, char **argv) { for (int i = 1; i + 1 < argc; i += 2) { gv_ladder_t l = gv_ladder((uint32_t)atoi(argv[i]), (uint32_t)atoi(argv[i + 1])); printf("%u", l.n); for (uint32_t k = 0; k < l.n; k++) printf(" %u %u %u", l.stop[k].w, l.stop[k].h, l.stop[k].scale_q16); printf("\n"); } return 0; }
'''
def c_ladders(sizes):
    d = Path(tempfile.mkdtemp()); (d / "l.c").write_text(LADDER_C); (d / "gallery_view.h").write_text((ROOT / "fw2/gallery/gallery_view.h").read_text())
    subprocess.run(["cc", "-O1", "-w", "-o", str(d / "l"), str(d / "l.c")], check=True, env=sdk_env())
    out = subprocess.run([str(d / "l")] + [str(v) for s in sizes for v in s], capture_output=True, text=True).stdout.splitlines()
    return [[tuple(int(x) for x in l.split()[1 + 3 * k:4 + 3 * k]) for k in range(int(l.split()[0]))] for l in out]

def harness(gfmt_c_text=None):
    d = Path(tempfile.mkdtemp()); h = (ROOT / "sim/gallery_fmt_harness.c").read_text().replace('#include "../fw2/gallery/gallery_fmt.c"', f'#include "{d / "gallery_fmt.c"}"')
    (d / "gallery_fmt.c").write_text(gfmt_c_text if gfmt_c_text is not None else (ROOT / "fw2/gallery/gallery_fmt.c").read_text()); (d / "gallery_fmt.h").write_text((ROOT / "fw2/gallery/gallery_fmt.h").read_text()); (d / "h.c").write_text(h)
    subprocess.run(["cc", "-O1", "-w", "-o", str(d / "h"), str(d / "h.c")], check=True, env=sdk_env()); return d / "h"
def runh(b, kind, path): return subprocess.run([str(b), kind, str(path)], capture_output=True, text=True).stdout.splitlines()

def grad(w, h):
    l = Image.linear_gradient("L"); r = l.resize((w, h)); g = l.transpose(Image.ROTATE_90).resize((w, h)); b = l.transpose(Image.ROTATE_180).resize((w, h))
    return Image.merge("RGB", (r, g, b))

def p3_profile():
    """A minimal ICC v2 matrix/TRC profile for Display P3 (D50-adapted primaries, gamma 2.2), built by hand so the test needs no external file."""
    s15 = lambda x: struct.pack(">i", int(round(x * 65536)))
    xyz = lambda x, y, z: b"XYZ \0\0\0\0" + s15(x) + s15(y) + s15(z)
    text = b"text\0\0\0\0test\0"; desc = b"desc\0\0\0\0" + struct.pack(">I", 8) + b"P3test\0\0" + struct.pack(">II", 0, 0) + struct.pack(">HB", 0, 0) + bytes(67)
    curv = b"curv\0\0\0\0" + struct.pack(">IH", 1, 0x0233) + b"\0\0"
    tags = [(b"desc", desc), (b"cprt", text), (b"wtpt", xyz(0.9642, 1.0, 0.8249)), (b"rXYZ", xyz(0.5151, 0.2412, -0.0011)), (b"gXYZ", xyz(0.2920, 0.6922, 0.0419)), (b"bXYZ", xyz(0.1571, 0.0666, 0.7841)), (b"rTRC", curv), (b"gTRC", curv), (b"bTRC", curv)]
    off = 128 + 4 + 12 * len(tags); table = struct.pack(">I", len(tags)); body = b""
    for sig, data in tags:
        data += bytes((-len(data)) % 4); table += sig + struct.pack(">II", off + len(body), len(data)); body += data
    size = off + len(body)
    head = struct.pack(">I4sI4s4s4s", size, b"\0\0\0\0", 0x02100000, b"mntr", b"RGB ", b"XYZ ") + bytes(12) + b"acsp" + bytes(4 + 4 + 4 + 4 + 8) + struct.pack(">I", 0) + s15(0.9642) + s15(1.0) + s15(0.8249) + bytes(4 + 16 + 28)
    head = head[:128].ljust(128, b"\0")
    return head + table + body

def verify():
    import tgy
    # 1. the ladder
    sizes = [(4000, 3000), (1600, 1200), (1000, 800), (800, 720), (600, 400), (4000, 600), (1700, 700), (6000, 4000), (2400, 1800), (801, 5000), (1, 1), (800, 721), (3000, 1440)]
    rnd = random.Random(7); sizes += [(rnd.randint(1, 9000), rnd.randint(1, 9000)) for _ in range(300)]
    cl = c_ladders(sizes)
    check(all(tgy.ladder(w, h) == cl[i] for i, (w, h) in enumerate(sizes)), "the Python ladder equals the firmware's gv_ladder on 313 sizes", next((sizes[i], tgy.ladder(*sizes[i]), cl[i]) for i in range(len(sizes)) if tgy.ladder(*sizes[i]) != cl[i]) if any(tgy.ladder(w, h) != cl[i] for i, (w, h) in enumerate(sizes)) else "")
    check([len(tgy.ladder(*s)) for s in sizes[:6]] == [3, 2, 2, 1, 1, 3], "the ladders of the six named sizes have 3, 2, 2, 1, 1 and 3 stops")
    # 2. tiles
    im = grad(300, 200); data, ntx, nty = tgy.tile_data(im)
    ok = (ntx, nty) == (3, 2) and len(data) == 6 * tgy.TILE_BYTES
    for ty in range(2):
        for tx in range(3):
            ok = ok and data[(ty * 3 + tx) * tgy.TILE_BYTES:(ty * 3 + tx + 1) * tgy.TILE_BYTES] == im.crop((tx * 128, ty * 128, tx * 128 + 128, ty * 128 + 128)).tobytes()
    last = data[5 * tgy.TILE_BYTES:]; pad_ok = all(last[(r * 128 + c) * 3:(r * 128 + c) * 3 + 3] == b"\0\0\0" for r in range(128) for c in range(128) if (256 + c >= 300 or 128 + r >= 200))
    check(ok, "tile_data: 6 tiles of 49,152 bytes, row-major, each the crop of the picture")
    check(pad_ok, "tile_data: the part of an edge tile past the picture is black")
    # 3. a file with levels: the half stop of a 2400 x 1800 picture is a raster level (option A, 1200 wide), the 1:1 stop a tile level (option B, wider than 2048)
    src = grad(2400, 1800); prep = tgy.prepare(src); canvas, rect, thumb, size = tgy.render_prepared(prep); levels = tgy.zoom_levels(prep)
    plain = tgy.pack_tgy(canvas, rect, thumb, size); full = tgy.pack_tgy(canvas, rect, thumb, size, levels)
    check(len(plain) == 1748992 and tgy.pack_tgy(canvas, rect, thumb, size, ()) == plain, "without levels the file is the M1 file (1,748,992 bytes)")
    desc = [(l["w"], l["h"], "raster") if isinstance(l, dict) else (l[0], l[1], "tiles") for l in levels]
    check(desc == [(1200, 900, "raster"), (2400, 1800, "tiles")], "a 2400 x 1800 picture gets a raster half level and a tile 1:1 level", desc)
    pa, pf = tgy.parse_tgy(plain), tgy.parse_tgy(full)
    check(len(pf["levels"]) == 3 and pf["levels"][0] == pa["levels"][0] and full[pa["levels"][0]["off"]:pa["levels"][0]["off"] + pa["levels"][0]["len"]] == plain[pa["levels"][0]["off"]:pa["levels"][0]["off"] + pa["levels"][0]["len"]], "level 0 (the fit canvas) is unchanged by the levels, in the table and in the bytes")
    lv = pf["levels"]
    check(all(l["off"] % 4096 == 0 for l in lv) and lv[1]["off"] >= lv[0]["off"] + lv[0]["len"] and lv[2]["off"] >= lv[1]["off"] + lv[1]["len"] and pf["size"] == len(full), "every level starts on a 4 KB boundary, in order, inside the file")
    check(lv[1]["layout"] == 0 and lv[1]["kind"] == 1 and lv[1]["stride"] == 3600 and lv[1]["len"] == 900 * 3600 and lv[1]["index_off"] == 0 and tuple(lv[1]["rect"]) == (0, 0, 1200, 900), "the raster level is layout 0, kind 1, stride 3,600, h rows, rect = the picture")
    check(lv[2]["layout"] == 1 and lv[2]["kind"] == 1 and lv[2]["stride"] == 384 and lv[2]["index_off"] == 0 and lv[0]["layout"] == 0 and lv[0]["kind"] == 0, "the tile level is layout 1, kind 1, stride 384, no index; the fit canvas is still layout 0, kind 0")
    try: [tgy.level_bytes(full, l) for l in lv]; crc_ok = True
    except ValueError as e: crc_ok = str(e)
    check(crc_ok is True, "every level's checksum holds", crc_ok)
    half = prep.resize((1200, 900), Image.LANCZOS)
    check(tgy.row_of(full, lv[1], 450) == half.tobytes()[450 * 3600:451 * 3600] and tgy.row_of(full, lv[1], 899) == half.tobytes()[899 * 3600:900 * 3600], "a raster row is where the stride says (rows 450 and 899)")
    check(tgy.tile_of(full, lv[2], 18, 14) == prep.crop((2304, 1792, 2432, 1920)).tobytes() and tgy.tile_of(full, lv[2], 3, 2) == prep.crop((384, 256, 512, 384)).tobytes(), "a tile is where the formula says (checked at two corners, one of them an edge tile)")
    # raster levels narrower than the screen are padded onto an 800 x 720 canvas, centred with even offsets; an odd width pads each row to 4 bytes
    narrow = tgy.raster_level(grad(700, 1500), 700, 1500)
    check((narrow["w"], narrow["h"], tuple(narrow["rect"]), narrow["stride"]) == (800, 1500, (50, 0, 700, 1500), 2400) and narrow["data"][:150] == bytes(150) and narrow["data"][150:153] != b"\0\0\0", "a 700-pixel-wide level is padded onto an 800-pixel canvas, 50 pixels of black on the left", (narrow["w"], narrow["h"], narrow["rect"]))
    short = tgy.raster_level(grad(1000, 500), 1000, 500)
    check((short["w"], short["h"], tuple(short["rect"])) == (1000, 720, (0, 110, 1000, 500)) and short["data"][:1000 * 3 * 110] == bytes(1000 * 3 * 110), "a 500-row level is padded to 720 rows, 110 black rows above")
    odd = tgy.raster_level(grad(1001, 801), 1001, 801)
    check(odd["stride"] == 3004 and len(odd["data"]) == 801 * 3004 and odd["data"][3003:3004] == b"\0" and odd["data"][3004 + 3003:3004 + 3004] == b"\0", "an odd width pads each row to a multiple of 4 bytes (3,003 becomes 3,004)")
    # 4 and 5. the C reader
    d = Path(tempfile.mkdtemp()); (d / "full.tgy").write_bytes(full); (d / "plain.tgy").write_bytes(plain)
    hb = harness(); out = runh(hb, "zoom", d / "full.tgy")
    r1, r2 = lv[1], lv[2]; ntx2, nty2 = (r2["w"] + 127) // 128, (r2["h"] + 127) // 128
    want = [f"zoom 0 rc 0 layout 0 w 1200 h 900 off {r1['off']} len {r1['len']} stride 3600 rect 0 0 1200 900 scale {(1200 * 65536 + size[0] // 2) // size[0]} first {r1['off']} last {r1['off'] + 899 * 3600} outside 4294967295 tileoff 4294967295",
            f"zoom 1 rc 0 layout 1 w 2400 h 1800 off {r2['off']} len {r2['len']} stride 384 ntx {ntx2} nty {nty2} scale {(2400 * 65536 + size[0] // 2) // size[0]} first {r2['off']} last {r2['off'] + (ntx2 * nty2 - 1) * 49152} right {r2['off'] + (ntx2 - 1) * 49152} outside 4294967295", "zoom 2 rc 5"]
    check(out == want, "the C reader reports the raster level and the tile level exactly as the Python parse, then 'no more levels'", (out, want))
    check(runh(hb, "zoom", d / "plain.tgy") == ["zoom 0 rc 5"], "a file without levels has no zoom level (rc 5)")
    t1, t2 = runh(hb, "tgy", d / "full.tgy"), runh(hb, "tgy", d / "plain.tgy")
    check(t1[0] == "rc 0" and t1[1].split("canvas_off")[1].split()[0] == t2[1].split("canvas_off")[1].split()[0], "the M1 reader (gt_parse) still finds the same fit canvas in a file with levels", (t1, t2))
    def damage(fn, line=0):
        b = bytearray(full); fn(b); p = d / "dmg.tgy"; p.write_bytes(bytes(b)); return runh(hb, "zoom", p)[line]
    le = lambda k: tgy.TGY_HEADER + k * tgy.LEVEL_ENTRY
    put = lambda k, off, v, fmt="<I": (lambda b: struct.pack_into(fmt, b, le(k) + off, v))
    check(damage(put(1, 28, lv[1]["len"] - 3600)) == "zoom 0 rc 6", "a raster level whose length is not exactly its rows is refused (rc 6)")
    check(damage(put(1, 20, 3601)) == "zoom 0 rc 6", "a raster stride that is not ceil(w * 3 / 4) * 4 is refused")
    check(damage(put(1, 24, lv[1]["off"] + 4)) == "zoom 0 rc 6", "a raster offset that is not 4 KB aligned is refused")
    check(damage(put(1, 0, 700)) == "zoom 0 rc 6" and damage(put(1, 4, 700)) == "zoom 0 rc 6", "a raster level narrower than 800 or shorter than 720 is refused")
    check(damage(put(1, 12, 1201, "<H")) == "zoom 0 rc 6" and damage(put(1, 12, 0, "<H")) == "zoom 0 rc 6", "a raster rect that leaves the canvas, or has no width, is refused")
    check(damage(put(2, 28, lv[2]["len"] - 49152), 1) == "zoom 1 rc 6", "a tile level whose length is not exactly its tiles is refused (rc 6)")
    check(damage(put(2, 20, 385), 1) == "zoom 1 rc 6", "a tile row that is not 384 bytes is refused")
    check(damage(put(2, 24, lv[2]["off"] + 4), 1) == "zoom 1 rc 6", "a tile offset that is not 4 KB aligned is refused")
    check(damage(put(2, 24, len(full) - 4096), 1) == "zoom 1 rc 6" and damage(put(2, 24, len(full) - 4096), 0).startswith("zoom 0 rc 0"), "a level that runs past the end of the file is refused (rc 6), the intact one before it still reads")
    check(damage(put(2, 0, 0), 1) == "zoom 1 rc 6" and damage(put(2, 0, 20000), 1) == "zoom 1 rc 6", "a tile level 0 or over 16,384 pixels wide is refused")
    fake = tgy.pack_tgy(canvas, rect, thumb, (20000, 128), [(20000, 128, bytes(157 * tgy.TILE_BYTES))]); (d / "wide.tgy").write_bytes(fake)
    check(runh(hb, "zoom", d / "wide.tgy") == ["zoom 0 rc 6"], "a level 20,000 pixels wide whose length is consistent is still refused (the 16,384 limit, rc 6)")
    fake = tgy.pack_tgy(canvas, rect, thumb, (3000, 2000), [(1003, 128, bytes(8 * tgy.TILE_BYTES))]); (d / "scale.tgy").write_bytes(fake)
    check(" scale 21911 " in runh(hb, "zoom", d / "scale.tgy")[0], "the scale of a level is rounded, not cut: 1003 of 3000 is 21,911, not 21,910 (Q16)", runh(hb, "zoom", d / "scale.tgy")[0])
    check(damage(put(1, 16, 2, "<B")).startswith("zoom 0 rc 0 layout 1 w 2400"), "a level of a layout the reader does not know (2) is skipped, the next zoom level is found")
    check(damage(lambda b: b.__setitem__(0, 0x58)) == "zoom 0 rc 2", "a file that is not a TGY file is refused (rc 2)")
    nar = tgy.pack_tgy(canvas, rect, thumb, (700, 1500), [narrow]); (d / "nar.tgy").write_bytes(nar); o2 = runh(hb, "zoom", d / "nar.tgy")[0]
    check(o2.startswith("zoom 0 rc 0 layout 0 w 800 h 1500") and " rect 50 0 700 1500 " in o2 and " scale 65536 " in o2, "the padded 700-pixel level reads: storage 800 x 1500, rect 50 0 700 1500, scale 1.0", o2)
    # 6. the converter
    w = Path(tempfile.mkdtemp()); s = w / "src"; s.mkdir()
    grad(2400, 1800).save(s / "big.jpg", quality=80); grad(1000, 800).save(s / "mid.jpg", quality=80); grad(600, 400).save(s / "small.jpg", quality=80)
    def conv(extra):
        o = w / ("out" + str(len(extra))); r = subprocess.run([sys.executable, str(ROOT / "tools/gallery/tgy_convert.py"), str(s), str(o)] + extra, capture_output=True, text=True)
        return r, [json.loads(l) for l in r.stdout.splitlines()], o
    r, ev, o = conv([]); lv0 = {e["file"]: e["levels"] for e in ev if e["event"] == "converted"}
    check(r.returncode == 0 and all(v == [] for v in lv0.values()) and all((o / f"{n}.tgy").stat().st_size == 1748992 for n in ("big", "mid", "small")), "by default no levels are written (every file stays 1,748,992 bytes)", lv0)
    r, ev, o = conv(["--levels"]); lv1 = {e["file"]: e["levels"] for e in ev if e["event"] == "converted"}
    check(lv1 == {"big.jpg": [[1200, 900, "raster"], [2400, 1800, "tiles"]], "mid.jpg": [[1000, 800, "raster"]], "small.jpg": []}, "--levels: a raster half level and a tile 1:1 level for the big picture, a raster 1:1 level for 1000 x 800, none for a picture smaller than the screen", lv1)
    sizes_ok = True
    for n, nl in (("big", 2), ("mid", 1), ("small", 0)):
        data = (o / f"{n}.tgy").read_bytes(); p = tgy.parse_tgy(data)
        if len(p["levels"]) != 1 + nl: sizes_ok = False
        for l in p["levels"]: tgy.level_bytes(data, l)
    idx = tgy.parse_index((o / "tau-library.tdb").read_bytes())
    check(sizes_ok and all(e["size"] == (o / (Path(e["path"]).name)).stat().st_size for e in idx), "the converted files parse, every level checksum holds, and the index records the real file sizes")
    # 7. colour
    prof = p3_profile(); p3 = Image.new("RGB", (64, 64), (200, 100, 50)); buf = io.BytesIO(); p3.save(buf, "PNG", icc_profile=prof); p3 = Image.open(io.BytesIO(buf.getvalue()))
    conv_px = tgy.to_srgb(p3).getpixel((5, 5)); diff = max(abs(a - b) for a, b in zip(conv_px, (200, 100, 50)))
    check(diff >= 8, "a Display P3 picture is converted to sRGB (a saturated colour moves by 8 levels or more)", conv_px)
    from PIL import ImageCms
    buf = io.BytesIO(); Image.new("RGB", (8, 8), (200, 100, 50)).save(buf, "PNG", icc_profile=ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()); sr = Image.open(io.BytesIO(buf.getvalue()))
    check(tgy.to_srgb(sr).getpixel((1, 1)) == (200, 100, 50), "a picture that is already sRGB is not touched")
    buf = io.BytesIO(); Image.new("RGB", (8, 8), (200, 100, 50)).save(buf, "PNG", icc_profile=b"not a profile at all"); bad = Image.open(io.BytesIO(buf.getvalue()))
    check(tgy.to_srgb(bad).getpixel((1, 1)) == (200, 100, 50) and tgy.to_srgb(Image.new("RGB", (4, 4), (1, 2, 3))).getpixel((0, 0)) == (1, 2, 3), "a broken profile and no profile leave the pixels alone")
    a = tgy.render(p3)[0]; b = tgy.render(Image.new("RGB", (64, 64), (200, 100, 50)))[0]
    check(a != b, "render() applies the conversion (the same pixels with and without the profile give different canvases)")

def mutants():
    global fails
    tg = (ROOT / "tools/gallery/tgy.py").read_text(); gf = (ROOT / "fw2/gallery/gallery_fmt.c").read_text()
    py = [("fq = min(CANVAS_W * Q16 // sw, CANVAS_H * Q16 // sh, Q16)", "fq = min(CANVAS_W * Q16 // sw, CANVAS_H * Q16 // sh)"),
          ("for ty in range(nty):\n        for tx in range(ntx):", "for tx in range(ntx):\n        for ty in range(nty):"),
          ("entries.append(struct.pack(\"<IIHHHHBBBBIIIIIII\", lw, lh, 0, 0, lw, lh, LAYOUT_TILE128", "entries.append(struct.pack(\"<IIHHHHBBBBIIIIIII\", lw, lh, 0, 0, lw, lh, LAYOUT_RASTER"),
          ("    return to_srgb(ImageOps.exif_transpose(img)).convert(\"RGB\")", "    return ImageOps.exif_transpose(img).convert(\"RGB\")")]
    cc = [("|| (off & 4095u) ||", "||"), ("if (len != ntx * nty * GT_TILE_BYTES) return GF_RANGE;", ""), ("stride != GT_TILE * 3u", "0"), ("w > 16384u || h > 16384u", "0"),
          ("(uint32_t)(((uint64_t)cw * 65536u + width / 2u) / width)", "(uint32_t)(((uint64_t)cw * 65536u) / width)"), ("off + (ty * z->ntx + tx) * GT_TILE_BYTES", "off + (tx * z->nty + ty) * GT_TILE_BYTES"),
          ("(e[16] != 0u && e[16] != 1u) ||", ""), ("len > fsize - off) return GF_RANGE;\n        z->layout", ") return GF_RANGE;\n        z->layout"),
          ("stride != ((w * 3u + 3u) & ~3u) || len != stride * h", "0"), ("len != stride * h", "0"), ("rw == 0u || rh == 0u || rx + rw > w || ry + rh > h", "0"), ("w < GT_CANVAS_W || h < GT_CANVAS_H || ", ""), ("z->off + row * z->stride", "z->off + row * z->stride + 4")]
    bad = 0
    for kind, muts in (("py", py), ("c", cc)):
        for i, (a, b) in enumerate(muts):
            base = tg if kind == "py" else gf
            if a not in base: print("MISSING mutant text", kind, i, a[:50]); bad += 1; continue
            d = Path(tempfile.mkdtemp())
            if kind == "py":
                (d / "tgy.py").write_text(base.replace(a, b, 1)); (d / "tgy_convert.py").write_text((ROOT / "tools/gallery/tgy_convert.py").read_text())
                env_src = str(d)
            fails = 0
            import io as _io, contextlib
            try:
                with contextlib.redirect_stdout(_io.StringIO()):
                    if kind == "py":
                        sys.modules.pop("tgy", None); sys.path.insert(0, str(d)); verify(); sys.path.pop(0); sys.modules.pop("tgy", None); sys.path.insert(0, str(ROOT / "tools/gallery"))
                    else:
                        global harness
                        orig = harness; harness = lambda t=None, _o=orig, _m=base.replace(a, b, 1): _o(_m); verify(); harness = orig
            except Exception as e: fails += 1
            print("SURVIVED" if fails == 0 else "killed  ", kind, i, a[:60]); bad += fails == 0
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    verify(); print("PASSED" if not fails else f"FAILED {fails}"); sys.exit(1 if fails else 0)
