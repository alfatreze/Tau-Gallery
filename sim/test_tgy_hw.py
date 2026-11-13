#!/usr/bin/env python3
"""Tests the TGY hardware driver (fw2/gallery/tgy_hw.c: container v1 reader + register driver) against a model of the decoder hardware (sim/tgy_hw_harness.c, which decodes with the C reference pipeline over a byte
addressed memory). Container files are written by the Python tools (tools/gallery/tgy_make.py); the picture the driver produces in the memory must equal the reference decode of the file (tgy_container.Container.pixels,
the TGY profile) byte for byte, in the right place: the fit level at its rectangle of the 800 x 720 canvas (stride 2,400, the rest untouched), the full level at an odd start address with an odd pitch (every tile
of a picture that is not a multiple of 128, partial edge tiles included). Also: header checksum, tile index checksum, an odd file base, a core without the decoder, a decoder error, a busy decoder, a start
rejected. `--mutants` edits copies of the driver; every edit must fail. Run: python3 sim/test_tgy_hw.py [--mutants]"""
import os, re, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_container as C, tgy_make as MK, test_jpegcoef as TJ
from PIL import Image
fails = 0
def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1
TMP = Path(tempfile.mkdtemp())
ENV = dict(os.environ); _sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
if _sdk: ENV["SDKROOT"] = _sdk
def build(src=None):
    exe = TMP / ("h_%d" % (abs(hash(src)) if src else 0))
    if src: (TMP / "drv").mkdir(exist_ok=True); [ (TMP / "drv" / n).write_text(t) for n, t in src.items() ]
    cmd = ["cc", "-O1", "-g", "-std=gnu99", "-Wall", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-o", str(exe), str(ROOT / "sim/tgy_hw_harness.c")]
    if src: cmd += ["-I", str(TMP / "drv")]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV)
    if r.returncode: print(r.stderr[:3000]); raise SystemExit("harness build failed")
    return exe
def run(exe, mode, data, base=0x1000, extra=()):
    f = TMP / "pic.tgy"; f.write_bytes(data); out = TMP / "out.bin"
    r = subprocess.run([str(exe), mode, str(f), str(base), str(out), *[str(x) for x in extra]], capture_output=True, text=True, timeout=300)
    lines = {l.split()[0]: l for l in r.stdout.splitlines()}
    return r.stdout, (out.read_bytes() if out.exists() and out.stat().st_size else b"")
def image(w, h, seed, grey=False):
    img = TJ.picture(w, h, seed=seed); return img.convert("L") if grey else img
def suite(exe):
    # 1. a 1000 x 900 picture: a fit level (on the canvas) and the full level (8 x 8 tiles... 1000 / 128 = 8 columns, 900 / 128 = 8 rows, partial edges)
    data = MK.from_image(image(1000, 900, 21)); c = C.Container(data)
    fit = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FIT][0]; full = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FULL][0]
    out, got = run(exe, "show", data)
    e = c.levels[fit]; rx, ry, rw, rh = e["rect"]; want = c.pixels(fit); canvas = np.frombuffer(got, np.uint8).reshape(720, 2400)
    region = canvas[ry:ry + e["h"], rx * 3:(rx + e["w"]) * 3].reshape(e["h"], e["w"], 3)
    check("fit level: the picture on the canvas equals the reference decode", np.array_equal(region, want))
    mask = np.ones((720, 2400), bool); mask[ry:ry + e["h"], rx * 3:(rx + e["w"]) * 3] = False
    check("fit level: every byte outside its rectangle untouched (0x55)", bool((canvas[mask] == 0x55).all()))
    check("fit level: driver ran clean (order errors 0, one table load)", "run 0" in out and "order 0" in out and "loads 1" in out)
    out, got2 = run(exe, "reloc", data)
    check("tiles decoded from streams copied to odd addresses of their own (the zoom's staging) equal the in-place decode, original streams wiped", "run 0" in out and got2 == run(exe, "show", data)[1] and len(got2) == 2400 * 720)
    out, got = run(exe, "sliced", data); full_out, full_got = run(exe, "full", data); ntiles = c.levels[full]["cols"] * c.levels[full]["rows"] if "cols" in c.levels[full] else ((c.levels[full]["w"] + 127) // 128) * ((c.levels[full]["h"] + 127) // 128)
    check("an index kept on the card: every tile's entry through the entry reader equals the resident index (offset, length and result), one reader call per tile", f"sliced same {ntiles} of {ntiles} calls {ntiles}" in out)
    check("sliced: a tile outside the level (column, row, huge) is TH_RANGE (5) and a level with neither index nor reader is refused (5)", "range 5 5 5 noidx 5" in out)
    check("sliced: a failing entry read is TH_SHORT (1), not a guess", "fetchfail 1" in out)
    check("sliced: the whole-index checksum is verified in pieces (4,096 and an odd 100 byte piece both pass), a failed read is TH_SHORT, a zero piece size TH_RANGE, a flipped bit in the first or the last byte TH_CRC (4), and the index passes again once restored",
          "verify 0 small 0 readfail 1 zerocap 5 lastbyte 4 firstbyte 4 again 0" in out)
    check("sliced: the whole level decodes through the entry reader to the same pixels as with the resident index", "run 0" in out and got == full_got and len(got) > 0)
    out, got = run(exe, "full", data); e = c.levels[full]; want = c.pixels(full); pitch = e["w"] * 3 + 5
    buf = np.frombuffer(got, np.uint8); rows = buf.reshape(e["h"] + 1, pitch)
    pic = rows[:e["h"], 1:1 + e["w"] * 3].reshape(e["h"], e["w"], 3)
    check("full level (%dx%d, %d tiles, odd start address, odd pitch): equals the reference decode" % (e["w"], e["h"], e["rows"] * e["cols"]), np.array_equal(pic, want))
    gap = rows[:, 1 + e["w"] * 3:1 + e["w"] * 3 + 4]
    check("full level: the bytes between rows and around the picture untouched", bool((gap == 0x55).all()) and bool((rows[:, 0] == 0x55).all()) and bool((rows[e["h"]] == 0x55).all()))
    # 2. a small picture: only the full level; and a grey one
    data = MK.from_image(image(300, 200, 22)); c = C.Container(data); out, got = run(exe, "full", data); e = c.levels[0]
    pic = np.frombuffer(got, np.uint8).reshape(e["h"] + 1, e["w"] * 3 + 5)[:e["h"], 1:1 + e["w"] * 3].reshape(e["h"], e["w"], 3)
    check("small picture (300x200, one level): full decode equals the reference", np.array_equal(pic, c.pixels(0)))
    data = MK.from_jpeg(TJ.encode(image(260, 190, 23, grey=True), quality=88)); c = C.Container(data); out, got = run(exe, "full", data); e = c.levels[0]
    pic = np.frombuffer(got, np.uint8).reshape(e["h"] + 1, e["w"] * 3 + 5)[:e["h"], 1:1 + e["w"] * 3].reshape(e["h"], e["w"], 3)
    check("grey picture (260x190): full decode equals the reference (grey on this core)", e["pixfmt"] == C.SUB_GREY and np.array_equal(pic, c.pixels(0)))
    # 2b. a second picture at the SAME base (the landing window is reused): its tables differ, the decoder must get them
    dA = MK.from_image(image(900, 760, 31), quality=95, fit_quality=95); dB = MK.from_image(image(900, 760, 32), quality=30, fit_quality=30); cA, cB = C.Container(dA), C.Container(dB)
    check("the two pictures have different quantisation tables", cA.tables[cA.levels[0]["qy"]] != cB.tables[cB.levels[0]["qy"]])
    f2 = TMP / "pic2.tgy"; f2.write_bytes(dB); (TMP / "pic.tgy").write_bytes(dA); out2 = TMP / "out2.bin"
    r = subprocess.run([str(exe), "two", str(TMP / "pic.tgy"), "4096", str(out2), str(f2)], capture_output=True, text=True, timeout=300)
    e = cB.levels[0] if cB.levels[0]["kind"] == C.KIND_FULL else cB.levels[-1]
    fitB = [i for i, x in enumerate(cB.levels) if x["kind"] == C.KIND_FIT]
    if fitB:
        e = cB.levels[fitB[0]]; rx, ry = e["rect"][0], e["rect"][1]; canvas = np.frombuffer(out2.read_bytes(), np.uint8).reshape(720, 2400)
        check("second file at the same base: its own tables are used (fit level equals its reference)", np.array_equal(canvas[ry:ry + e["h"], rx * 3:(rx + e["w"]) * 3].reshape(e["h"], e["w"], 3), cB.pixels(fitB[0])))
    else: check("second file has a fit level", False)
    # 2c. the fit level and the full level of one file have different tables: the driver must reload between them
    data = MK.from_image(image(1000, 900, 21)); c = C.Container(data); fit = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FIT][0]; full = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FULL][0]
    check("the fit and full levels use different tables", c.levels[fit]["qy"] != c.levels[full]["qy"] or c.tables[c.levels[fit]["qy"]] != c.tables[c.levels[full]["qy"]])
    out, got = run(exe, "both", data); e = c.levels[fit]; rx, ry = e["rect"][0], e["rect"][1]; plane = np.frombuffer(got[:2400 * 720], np.uint8).reshape(720, 2400)
    check("both levels of one file: the fit level is right", np.array_equal(plane[ry:ry + e["h"], rx * 3:(rx + e["w"]) * 3].reshape(e["h"], e["w"], 3), c.pixels(fit)))
    e = c.levels[full]; rows = np.frombuffer(got[2400 * 720:], np.uint8).reshape(e["h"] + 1, e["w"] * 3 + 5)
    check("both levels of one file: the full level is right (the tables were reloaded)", np.array_equal(rows[:e["h"], 1:1 + e["w"] * 3].reshape(e["h"], e["w"], 3), c.pixels(full)) and "loads 2" in out)
    out, _ = run(exe, "showfull", data); check("a level wider than the canvas is refused by show (TH_RANGE, 5) with no start", "run 5" in out and "starts 0" in out)
    out, got = run(exe, "retry", data); e = c.levels[fit]; plane = np.frombuffer(got, np.uint8).reshape(720, 2400)
    check("a rejected start returns TH_BUSY (9); the retry works (CLEAR removed the sticky flag) and the tile is right", "first 9" in out and "run 0" in out and np.array_equal(plane[ry:ry + 128, rx * 3:(rx + 128) * 3].reshape(128, 128, 3), c.pixels(fit)[:128, :128]))
    d444 = MK.from_jpeg(TJ.encode(image(700, 500, 24), quality=90, subsampling=0)); c444 = C.Container(d444); out, got = run(exe, "full", d444); e = c444.levels[0]
    pic = np.frombuffer(got, np.uint8).reshape(e["h"] + 1, e["w"] * 3 + 5)[:e["h"], 1:1 + e["w"] * 3].reshape(e["h"], e["w"], 3)
    check("a 4:4:4 level (K6, the decoder is built with C444) is kept as it is and decodes to the reference", e["pixfmt"] == C.SUB_444 and "run 0" in out and np.array_equal(pic, c444.pixels(0)))
    # 2d. only the first part of the file is resident (the fit level comes first): the fit level shows, the full level is not offered
    data = MK.from_image(image(1000, 900, 21)); c = C.Container(data); fit = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FIT][0]; e = c.levels[fit]
    end = max(e["data_off"] + e["data_len"], e["index_off"] + e["index_len"]); end = (end + 4095) // 4096 * 4096
    check("the fit level lies entirely before the full level in the file", end <= min(x["data_off"] for x in c.levels if x["kind"] == C.KIND_FULL) and end < len(data))
    out, got = run(exe, "prefix", data, extra=(end,)); rx, ry = e["rect"][0], e["rect"][1]; plane = np.frombuffer(got, np.uint8).reshape(720, 2400)
    check("a prefix of %d of %d bytes: the fit level equals the reference, the full level is not offered (find 6)" % (end, len(data)), "full 6" in out and "run 0" in out and np.array_equal(plane[ry:ry + e["h"], rx * 3:(rx + e["w"]) * 3].reshape(e["h"], e["w"], 3), c.pixels(fit)))
    out, _ = run(exe, "prefix", data, extra=(len(data) + 4096,)); check("n larger than the file size is refused (open 5)", "open 5" in out)
    # 3. refusals
    data = MK.from_image(image(1000, 900, 21))
    out, _ = run(exe, "badhdr", data); check("a damaged header is refused with TH_CRC (4)", "open 4" in out)
    out, _ = run(exe, "badidx", data); check("a damaged tile index is refused with TH_CRC (4)", "open 0" in out and "index 4" in out)
    out, _ = run(exe, "odd", data); check("an odd file base is refused (TH_ODD, 12) before any register is touched", "run 12" in out and "starts 0" in out and "loads 0" in out)
    out, _ = run(exe, "absent", data); check("a core without the decoder gives TH_ABSENT (8)", "run 8" in out and "starts 0" in out)
    out, _ = run(exe, "busy", data); check("a busy decoder gives TH_BUSY (9) and the driver stops", "run 9" in out)
    out, _ = run(exe, "hwerr", data); check("a decoder error gives TH_HWERR (11) with the error code in the status", "run 11" in out and re.search(r"status 8000010[0-9a-f]", out) is not None)
    out, _ = run(exe, "show", data, base=0x1001); check("an odd base on open is refused (TH_ODD, 12)", "open 12" in out)
MUTANTS = [
    ("tile origin x off by one pixel", "tx * TH_TILE * 3u", "tx * TH_TILE * 2u"), ("pitch ignored in the row step", "ty * TH_TILE * pitch", "ty * TH_TILE * 2400u"),
    ("tile height not clipped", "(l->h - y0 < TH_TILE) ? l->h - y0 : TH_TILE", "TH_TILE"), ("tile width not clipped", "(l->w - x0 < TH_TILE) ? l->w - x0 : TH_TILE", "TH_TILE"),
    ("stream offset not added", "*off = l->data_off + o;", "*off = l->data_off;"), ("table address of Cb/Cr wrong", "128u * l->qc;\n    if ((qy | qc) & 1u)", "128u * l->qy;\n    if ((qy | qc) & 1u)"),
    ("tables skipped when cached wrongly (always)", "if (qy == g_loaded_y && qc == g_loaded_c) return TH_OK;", "if (g_loaded_y != 0xFFFFFFFFu) return TH_OK;"),
    ("index CRC not compared", "if (gf_crc32(buf, l->idx_len) != l->idx_crc) return TH_CRC;", ""),
    ("a failed entry read is ignored", "    else return TH_SHORT;", "    else ent = tmp;"),
    ("entry position by column then row", "l->fetch(l->fctx, l->idx_off + (ty * l->cols + tx) * TH_IDX, tmp)", "l->fetch(l->fctx, l->idx_off + (tx * l->rows + ty) * TH_IDX, tmp)"),
    ("a card-resident level is refused", "(!l->idx && !l->fetch))", "(!l->idx))"),
    ("verify: the pieces are not summed", "c = gf_crc32_step(c, buf, n); pos += n;", "pos += n;"),
    ("verify: the result is not compared", "return ~c == l->idx_crc ? TH_OK : TH_CRC;", "return TH_OK;"),
    ("verify: a failed read is ignored", "if (!rd(ctx, l->idx_off + pos, n, buf)) return TH_SHORT;", "rd(ctx, l->idx_off + pos, n, buf);"),
    ("verify: the last piece is a whole piece", "const uint32_t n = l->idx_len - pos < cap ? l->idx_len - pos : cap;", "const uint32_t n = cap;"),
    ("verify: every piece read from the start of the index", "rd(ctx, l->idx_off + pos, n, buf)", "rd(ctx, l->idx_off, n, buf)"),
    ("header CRC not checked", "if (gf_crc32(img, 52u) != th32(img + 52)) return TH_CRC;", ""),
    ("hardware error ignored", "if (g_status & 4u) return TH_HWERR;", ""),
    ("rejected start ignored", "if (g_status & 0x10u) return TH_BUSY;", ""),
    ("tables cache not dropped for a new file", "    tgy_hw_forget();                                                              /* a new file (the landing window is reused for every picture): the decoder's tables are stale */\n", ""),
    ("odd base accepted", "if (n > size || (base & 1u)) return base & 1u ? TH_ODD : TH_RANGE;", "if (n > size) return TH_RANGE;"),
    ("n larger than the file accepted", "if (n > size || (base & 1u)) return base & 1u ? TH_ODD : TH_RANGE;", "if (base & 1u) return TH_ODD;"),
    ("a level beyond the resident bytes offered", "    if (ilen != rows * cols * TH_IDX || ioff < TH_HEAD || ioff > f->n || ilen > f->n - ioff) return TH_RANGE;\n    if (doff < TH_HEAD || doff > f->n || dlen > f->n - doff) return TH_RANGE;", "    if (ilen != rows * cols * TH_IDX) return TH_RANGE;"),
    ("absent core not detected", "if (!tgy_hw_present()) return TH_ABSENT;", ""),
    ("START without a separate CLEAR store first", "REG_WR(R_TGY_CTRL, 8u);                                                        /* CLEAR", "/* CLEAR"),
    ("level larger than the canvas accepted on show", "if ((uint32_t)l->rx + l->w > GT_CANVAS_W || (uint32_t)l->ry + l->h > GT_CANVAS_H) return TH_RANGE;", ""),
    ("index read failure ignored", "if (!rd(ctx, l->idx_off, l->idx_len, buf)) return TH_SHORT;", ""),
    ("stream length halved", "REG_WR(R_TGY_LEN, len);", "REG_WR(R_TGY_LEN, len / 2u);"),
    ("explicit stream address ignored (the file's own position is used)", "REG_WR(R_TGY_SRC, src);", "REG_WR(R_TGY_SRC, f->base + foff);"),
    ("the wrapper passes the wrong stream address", "return tgy_hw_tile_from(f, l, tx, ty, dst, pitch, f->base + foff);", "return tgy_hw_tile_from(f, l, tx, ty, dst, pitch, f->base + l->data_off);"),
    ("tile location ignores the level's data offset", "*off = l->data_off + o; *len = n;", "*off = o; *len = n;"),
]
def main():
    global fails
    if "--mutants" in sys.argv:
        src = {n: (ROOT / "fw2/gallery" / n).read_text() for n in ("tgy_hw.c", "tgy_hw.h", "gallery_fmt.c", "gallery_fmt.h", "tgy_dec.c", "tgy_dec.h", "tgy_pix.c", "tgy_pix.h")}
        survived = 0
        for name, a, b in MUTANTS:
            assert a in src["tgy_hw.c"], name
            m = dict(src); m["tgy_hw.c"] = src["tgy_hw.c"].replace(a, b, 1)
            # the harness includes ../fw2/gallery/*.c; a mutant tree mirrors that layout
            d = TMP / ("mut_" + str(abs(hash(name)))); (d / "fw2/gallery").mkdir(parents=True); (d / "fw2/platform").mkdir(); (d / "sim").mkdir()
            for n, t in m.items(): (d / "fw2/gallery" / n).write_text(t)
            (d / "fw2/platform/mmio.h").write_text((ROOT / "fw2/platform/mmio.h").read_text()); (d / "sim/tgy_hw_harness.c").write_text((ROOT / "sim/tgy_hw_harness.c").read_text())
            exe = d / "h"; r = subprocess.run(["cc", "-O1", "-std=gnu99", "-Wno-unused-function", "-o", str(exe), str(d / "sim/tgy_hw_harness.c")], capture_output=True, text=True, env=ENV)
            if r.returncode: print(r.stderr); raise SystemExit("mutant build failed: " + name)
            before = fails; import io, contextlib
            try:
                with contextlib.redirect_stdout(io.StringIO()): suite(exe)
            except (ValueError, IndexError, AssertionError): fails += 1                      # a mutant that writes outside the picture breaks the shapes: killed
            killed = fails > before; fails = before; print(("killed   " if killed else "SURVIVED ") + name); survived += 0 if killed else 1
        sys.exit(1 if survived else 0)
    suite(build()); print("tgy-hw: %d failure(s)" % fails); sys.exit(1 if fails else 0)
main()
