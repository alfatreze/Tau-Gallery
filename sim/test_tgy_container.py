#!/usr/bin/env python3
"""Tests of the TGY container version 1 (tools/gallery/tgy_container.py, tgy_make.py, tgy_encode.py; docs/TGY_CONTAINER_V1.md): lossless full levels from JPEGs (the coefficients come back identical, the pixels equal the TGY
profile of the original), encoded levels from pixel sources, geometry and 4 KB alignment, standalone decoding of any single tile in any order, and every kind of damage a reader must refuse. `--mutants` edits the reader and
writer; every edit must be caught."""
import io, random, struct, sys, types, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import jpegcoef, tgy_container as C, tgy_encode as E, tgy_make as MK, tgy_recon as R, tgy_v0 as V, test_jpegcoef as TJ
from PIL import Image

def jpeg(w, h, **kw): return TJ.encode(TJ.picture(w, h, seed=4), **kw)
def same_coefs(a, b): return all(np.array_equal(x.coef, y.coef) for x, y in zip(a.components, b.components))
def psnr(a, b):
    m = ((a.astype(float) - b.astype(float)) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
def fix_header_crc(buf):
    buf[52:56] = struct.pack("<I", C.crc(bytes(buf[:52]))); return bytes(buf)

class T(unittest.TestCase):
    def setUp(self):
        import signal
        def hang(s, f): raise TimeoutError("hung")
        signal.signal(signal.SIGALRM, hang); signal.alarm(300)
    def tearDown(self):
        import signal; signal.alarm(0)
    def test_jpeg_levels_are_lossless_and_pixel_identical(self):
        for data in (jpeg(1000, 700, quality=85, subsampling=2, progressive=True), jpeg(300, 200, quality=90, subsampling=0), jpeg(257, 131, quality=80, subsampling=2)):
            blob = MK.from_jpeg(data); c = C.Container(blob); j = jpegcoef.read_jpeg(data); full = [i for i, e in enumerate(c.levels) if e["kind"] == C.KIND_FULL][0]
            self.assertTrue(same_coefs(c.picture(full), j)); self.assertTrue(np.array_equal(c.pixels(full), R.decode_pixels_tgy(j)))
    def test_fit_level_first_and_good_quality(self):
        data = jpeg(1000, 700, quality=90, subsampling=2); c = C.Container(MK.from_jpeg(data)); self.assertEqual([e["kind"] for e in c.levels], [C.KIND_FIT, C.KIND_FULL])
        e = c.levels[0]; self.assertEqual((e["w"], e["h"], tuple(e["rect"])), (800, 560, (0, 80, 800, 560))); self.assertLess(e["data_off"], c.levels[1]["data_off"])
        src = np.asarray(Image.open(io.BytesIO(data)).convert("RGB").resize((800, 560), Image.LANCZOS)); self.assertGreater(psnr(c.pixels(0), src), 30.0)
    def test_small_picture_has_only_the_full_level(self):
        c = C.Container(MK.from_jpeg(jpeg(300, 200, quality=90, subsampling=2))); self.assertEqual([e["kind"] for e in c.levels], [C.KIND_FULL]); self.assertEqual(tuple(c.levels[0]["rect"]), (250, 260, 300, 200))
    def test_pixel_source_is_encoded(self):
        img = TJ.picture(500, 330, seed=6, patch=False); blob = MK.from_image(img, quality=90); c = C.Container(blob); self.assertEqual(c.width, 500)
        out = c.pixels(len(c.levels) - 1); self.assertEqual(out.shape, (330, 500, 3)); self.assertGreater(psnr(out, np.asarray(img)), 30.0)
        ref = R.decode_pixels_tgy(E.encode_rgb(np.asarray(img), 90, 2)); self.assertTrue(np.array_equal(out, ref))                      # the container adds nothing and loses nothing
    def test_grey(self):
        g = TJ.picture(300, 200, seed=7).convert("L"); data = TJ.encode(g, quality=88); c = C.Container(MK.from_jpeg(data)); self.assertEqual(c.channels, 1)
        self.assertTrue(np.array_equal(c.pixels(0), R.decode_pixels_tgy(jpegcoef.read_jpeg(data))))
        c2 = C.Container(MK.from_image(g, 88)); self.assertEqual(c2.channels, 3)                                                       # from_image converts to colour: grey JPEGs keep their grey path
    def test_orientation_is_recorded_and_the_fit_level_is_upright(self):
        img = TJ.picture(1000, 700, seed=8); ex = img.getexif(); ex[0x0112] = 6; b = io.BytesIO(); img.save(b, "JPEG", exif=ex, quality=88, subsampling=0); c = C.Container(MK.from_jpeg(b.getvalue()))
        self.assertEqual((c.orientation, c.width, c.height), (6, 700, 1000)); fit = c.levels[0]; self.assertEqual((fit["w"], fit["h"]), (504, 720))                  # upright 700 x 1000 fits the 800 x 720 canvas as 504 x 720
    def test_alignment_and_first_read(self):
        c = C.Container(MK.from_jpeg(jpeg(1000, 700, quality=85, subsampling=2))); self.assertEqual(len(c.data) % 4096, 0)
        self.assertLessEqual(C.HEADER + C.LEVEL * len(c.levels) + 2 + 128 * len(c.tables), 4096)                                          # header, level table and tables fit one 4 KB read
        for e in c.levels:
            self.assertEqual(e["data_off"] % 4096, 0); self.assertEqual(e["index_off"] % 4096, 0)
        self.assertEqual(struct.unpack_from("<I", c.data, 28)[0] % 4096, 0)
    def test_tiles_decode_alone_in_any_order(self):
        data = jpeg(300, 200, quality=85, subsampling=2); c = C.Container(MK.from_jpeg(data)); j = jpegcoef.read_jpeg(data); tiles = R.split_tiles(j); random.Random(1).shuffle(tiles)
        for ty, tx, grids, shapes in tiles: self.assertEqual(V.decode_tile(c.tile_stream(0, ty, tx), shapes), grids)                    # no other tile is touched
        self.assertEqual((c.levels[0]["rows"], c.levels[0]["cols"]), (2, 3))
    def test_raw_canvas_level(self):
        raw = bytes(range(256)) * (800 * 720 * 3 // 256) + bytes(800 * 720 * 3 % 256); thumb = bytes(C.THUMB_BYTES)
        c = C.Container(C.write_container(800, 720, 1, thumb, [dict(kind=C.KIND_FIT_RAW, raw=raw, rect=(0, 0, 800, 720))])); self.assertEqual(c.pixels(0).tobytes(), raw)
    def test_damage_is_refused(self):
        good = MK.from_jpeg(jpeg(300, 200, quality=85, subsampling=2)); C.Container(good)
        def bad(buf, msg):
            with self.assertRaises(ValueError, msg=msg):
                c = C.Container(bytes(buf)); [c.picture(i) for i in range(len(c.levels)) if c.levels[i]["supported"]]
        b = bytearray(good); b[0] = 0x58; bad(b, "magic"); b = bytearray(good); b[4] = 9; bad(fix_header_crc(b), "version"); b = bytearray(good); b[9] ^= 1; bad(b, "header crc")
        bad(good[:-4096], "truncated"); bad(good[:40], "short"); b = bytearray(good); b[24:28] = struct.pack("<I", len(good)); bad(fix_header_crc(b), "level table past the end")
        b = bytearray(good); b[40:44] = struct.pack("<I", len(good) - 8); bad(fix_header_crc(b), "tables past the end"); b = bytearray(good); b[20:24] = struct.pack("<I", 99); bad(fix_header_crc(b), "level count")
        c = C.Container(good); e = c.levels[0]
        b = bytearray(good); b[e["data_off"] + 7] ^= 0x40; bad(b, "tile stream flipped")                                                    # the tile CRC catches it
        b = bytearray(good); b[e["index_off"] + 3] ^= 1; bad(b, "index flipped")
        b = bytearray(good); idx = bytearray(good[e["index_off"]:e["index_off"] + e["index_len"]]); idx[4:8] = struct.pack("<I", 1 << 30); b[e["index_off"]:e["index_off"] + e["index_len"]] = idx
        lt = C.HEADER; b[lt + 44:lt + 48] = b[lt + 44:lt + 48]; ent = bytearray(b[lt:lt + C.LEVEL]); ent[40:44] = struct.pack("<I", C.crc(bytes(idx))); b[lt:lt + C.LEVEL] = ent; bad(b, "tile length past the data")
        b = bytearray(good); tab = struct.unpack_from("<I", good, 40)[0]; b[tab + 2:tab + 4] = b"\x00\x00"; bad(b, "zero quantisation entry")
        b = bytearray(good); ent = bytearray(b[lt:lt + C.LEVEL]); ent[20] = 7; b[lt:lt + C.LEVEL] = ent; bad(b, "quantisation table index of Y")
        b = bytearray(good); ent = bytearray(b[lt:lt + C.LEVEL]); ent[24:28] = struct.pack("<I", 1 << 31); b[lt:lt + C.LEVEL] = ent; bad(b, "data offset")
        b = bytearray(good); ent = bytearray(b[lt:lt + C.LEVEL]); ent[28:32] = struct.pack("<I", len(good) + 8192); b[lt:lt + C.LEVEL] = ent; bad(b, "data length past the end of the file")
        bad(good + bytes(4096), "trailing bytes: the size in the header is the size of the file")
        b = bytearray(good); idx = bytearray(good[e["index_off"]:e["index_off"] + e["index_len"]]); idx[0:12], idx[12:24] = idx[12:24], idx[0:12]; b[e["index_off"]:e["index_off"] + e["index_len"]] = idx; bad(b, "two index entries swapped: each tile CRC still fits its stream, only the index CRC sees it")
        b = bytearray(good); tail = good[e["data_off"] + struct.unpack_from("<I", good, e["index_off"])[0]:e["data_off"] + e["data_len"]]; idx = bytearray(good[e["index_off"]:e["index_off"] + e["index_len"]])
        idx[4:8] = struct.pack("<I", len(tail) + 1000); idx[8:12] = struct.pack("<I", C.crc(bytes(good[e["data_off"]:e["data_off"] + len(tail) + 1000]))); b[e["index_off"]:e["index_off"] + e["index_len"]] = idx; ent = bytearray(b[lt:lt + C.LEVEL]); ent[40:44] = struct.pack("<I", C.crc(bytes(idx))); b[lt:lt + C.LEVEL] = ent
        c2 = C.Container(bytes(b))
        with self.assertRaises(ValueError, msg="a tile whose length runs past the level data, with a CRC that fits the bytes it covers"): c2.tile_stream(0, 0, 0)
    def test_unknown_mode_is_flagged_not_fatal(self):
        b = bytearray(MK.from_jpeg(jpeg(300, 200, quality=85, subsampling=2))); lt = C.HEADER; b[lt + 17] = 2                                # mode 2 (TGX, reserved)
        c = C.Container(bytes(b)); self.assertFalse(c.levels[0]["supported"])
    def test_tile_range(self):
        c = C.Container(MK.from_jpeg(jpeg(300, 200, quality=85, subsampling=2)))
        for ty, tx in ((2, 0), (0, 3), (-1, 0)):
            with self.assertRaises(ValueError): c.tile_stream(0, ty, tx)
    def test_encoder_quality_and_tables(self):
        self.assertEqual(E.scaled_table(E.STD_LUMA, 50)[:4], [16, 11, 12, 14]); self.assertEqual(min(E.scaled_table(E.STD_LUMA, 100)), 1)
        img = np.asarray(TJ.picture(120, 90, seed=3, patch=False)); a = E.encode_rgb(img, 95, 2); b = E.encode_rgb(img, 40, 2)
        self.assertGreater(psnr(R.decode_pixels_tgy(a), img), psnr(R.decode_pixels_tgy(b), img)); self.assertGreater((a.components[0].coef != 0).sum(), (b.components[0].coef != 0).sum())

# Not mutated: the level-count bound and the tile-index-size check overlap with the table bounds and the index CRC (each damage they would catch is caught by those first), so no test can tell them apart.
def mutants():
    src = (ROOT / "tools/gallery/tgy_container.py").read_text()
    muts = [("header crc check", 'if crc(data[:52]) != struct.unpack_from("<I", data, 52)[0]: raise ValueError("header checksum")', "pass"), ("size check", 'if size != n: raise ValueError("file size differs from the header")', "pass"),
            ("version", "if ver != VERSION or hsz != HEADER:", "if hsz != HEADER:"), ("tile crc", 'if crc(s) != c: raise ValueError("tile checksum")', "pass"), ("tile range", "if off + ln > e[\"data_len\"]: raise ValueError(\"tile stream out of range\")", "pass"),
            ("index crc", 'if crc(data[e["index_off"]:e["index_off"] + e["index_len"]]) != e["index_crc"]: raise ValueError("tile index checksum")', "pass"),
            ("quant index", 'if e["qy"] >= nt or (e["pixfmt"] != SUB_GREY and e["qc"] >= nt): raise ValueError("quantisation table index out of range")', "pass"),
            ("zero quant", "if min(t) < 1: raise ValueError(\"quantisation table entry of zero\")", "pass"), ("data range", 'if e["data_off"] + e["data_len"] > n or e["data_off"] < HEADER: raise ValueError("level data out of range")', "pass"),
            ("tile order", "for ty, tx, grids, shapes in R.split_tiles(pic):", "for ty, tx, grids, shapes in reversed(R.split_tiles(pic)):"),
            ("align", "align = lambda n: (n + ALIGN - 1) // ALIGN * ALIGN", "align = lambda n: n"),
            ("supported test", '(e["layout"], e["mode"]) in ((LAYOUT_RASTER, MODE_RAW), (LAYOUT_TILES, MODE_TGY))', '(e["layout"], e["mode"]) in ((LAYOUT_RASTER, MODE_RAW), (LAYOUT_TILES, MODE_TGY), (LAYOUT_TILES, 2))'),
            ("grid", "return -(-bh // 16), -(-bw // 16)", "return -(-bh // 16) + 1, -(-bw // 16)")]
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("tgy_container"); sys.modules["tgy_container"] = mod
        try: exec(compile(src.replace(a, b, 1), "tgy_container_mut", "exec"), mod.__dict__)
        except Exception: print("killed   (does not run)", name); continue
        globals()["C"] = mod
        for m in ("tgy_make",):
            if m in sys.modules: sys.modules[m].C = mod
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
        print(("killed  " if not r.wasSuccessful() else "SURVIVED"), name); bad += r.wasSuccessful()
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    unittest.main()
