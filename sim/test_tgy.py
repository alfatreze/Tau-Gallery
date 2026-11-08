#!/usr/bin/env python3
"""Tests of the Tau Gallery M1 formats and converter (tools/gallery): the geometry of the fit canvas, the plane layout of the pixels, orientation, the thumbnail, the header and index checks, every kind of damage a reader
must refuse, and the CLI end to end. Mutants (--mutants) edit the library in memory and each must make a check fail."""
import io, json, struct, sys, tempfile, unittest, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy, tgy_convert
from PIL import Image

def gradient(w, h):
    im = Image.new("RGB", (w, h))
    im.putdata([((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (x + y) % 256) for y in range(h) for x in range(w)])
    return im

class T(unittest.TestCase):
    def test_fit_rect(self):
        self.assertEqual(tgy.fit_rect(1600, 1440), (0, 0, 800, 720))                          # exact 10:9 at twice the size
        self.assertEqual(tgy.fit_rect(3000, 2000), (0, 93, 800, 533))                        # landscape: full width, bars above and below
        x, y, w, h = tgy.fit_rect(2000, 3000)                                                # portrait: full height, bars at the sides
        self.assertEqual((y, h), (0, 720)); self.assertEqual(x % 2, 0); self.assertLessEqual(x + w, 800); self.assertEqual(w, 480)
        self.assertEqual(tgy.fit_rect(200, 100), (300, 310, 200, 100))                        # small: 1:1, centred, never enlarged
        self.assertEqual(tgy.fit_rect(200, 100, upscale=True), (0, 160, 800, 400))            # unless asked
        for w0, h0 in ((1, 1), (7, 3), (800, 720), (801, 720), (5472, 3648)):
            x, y, w, h = tgy.fit_rect(w0, h0); self.assertTrue(w >= 1 and h >= 1 and x >= 0 and y >= 0 and x + w <= 800 and y + h <= 720 and x % 2 == 0)
    def test_canvas_layout(self):
        img = gradient(800, 720); canvas, rect, thumb, size = tgy.render(img)
        self.assertEqual(len(canvas), 800 * 720 * 3); self.assertEqual(rect, (0, 0, 800, 720)); self.assertEqual(size, (800, 720))
        for (x, y) in ((0, 0), (799, 0), (0, 719), (799, 719), (400, 360), (123, 456)):
            o = (y * 800 + x) * 3; self.assertEqual(tuple(canvas[o:o + 3]), img.getpixel((x, y)))      # rows of 800 R, G, B bytes: the plane's own layout
    def test_letterbox_is_black(self):
        img = Image.new("RGB", (400, 100), (200, 100, 50)); canvas, rect, thumb, size = tgy.render(img)
        x, y, w, h = rect; self.assertEqual((w, h), (400, 100))
        self.assertEqual(tuple(canvas[((y - 1) * 800 + x) * 3:((y - 1) * 800 + x) * 3 + 3]), (0, 0, 0))   # above
        self.assertEqual(tuple(canvas[(y * 800 + x) * 3:(y * 800 + x) * 3 + 3]), (200, 100, 50))           # first picture pixel
        self.assertEqual(tuple(canvas[(y * 800 + x + w) * 3:(y * 800 + x + w) * 3 + 3]), (0, 0, 0))        # right of it
    def test_orientation_applied(self):
        img = Image.new("RGB", (300, 200), (0, 0, 0)); img.putpixel((0, 0), (255, 0, 0))
        ex = img.getexif(); ex[0x0112] = 6                                                   # rotate 90 clockwise to display: 200 x 300 and the red corner moves to the top right
        buf = io.BytesIO(); img.save(buf, "JPEG", exif=ex, quality=100, subsampling=0); buf.seek(0)
        canvas, rect, thumb, size = tgy.render(Image.open(buf))
        self.assertEqual(size, (200, 300)); x, y, w, h = rect; self.assertEqual((w, h), (200, 300))
        px = tuple(canvas[(y * 800 + x + w - 1) * 3:(y * 800 + x + w - 1) * 3 + 3]); self.assertGreater(px[0], 150); self.assertLess(px[1] + px[2], 100)
    def test_thumbnail(self):
        img = Image.new("RGB", (960, 720), (255, 0, 0)); canvas, rect, thumb, size = tgy.render(img)
        self.assertEqual(len(thumb), tgy.THUMB_BYTES)
        v = thumb[(36 * 96 + 48) * 2] | thumb[(36 * 96 + 48) * 2 + 1] << 8; self.assertEqual(v, 0xF800)    # red, little-endian RGB565
        self.assertEqual(tgy.rgb565_le(bytes([255, 255, 255, 0, 0, 255, 8, 4, 8])), bytes([0xFF, 0xFF, 0x1F, 0x00, 0x21, 0x08]))
    def test_roundtrip(self):
        img = gradient(1000, 700); canvas, rect, thumb, size = tgy.render(img); data = tgy.pack_tgy(canvas, rect, thumb, size)
        self.assertEqual(len(data) % 4096, 0)
        t = tgy.parse_tgy(data); self.assertEqual((t["width"], t["height"]), (1000, 700)); self.assertEqual(len(t["levels"]), 1)
        lv = t["levels"][0]; self.assertEqual((lv["w"], lv["h"], lv["stride"], lv["len"]), (800, 720, 2400, 1728000)); self.assertEqual(lv["off"] % 4096, 0); self.assertEqual(t["thumb"][0] % 4096, 0)
        self.assertEqual(tgy.level_bytes(data, lv), canvas); self.assertEqual(data[t["thumb"][0]:t["thumb"][0] + t["thumb"][1]], thumb)
    def test_damage_is_refused(self):
        canvas, rect, thumb, size = tgy.render(gradient(100, 100)); good = tgy.pack_tgy(canvas, rect, thumb, size); tgy.parse_tgy(good)
        for name, bad in (("magic", b"XGY\0" + good[4:]), ("version", good[:4] + b"\x09\x00" + good[6:]), ("size", good[:-4096]), ("header crc", good[:20] + bytes([good[20] ^ 1]) + good[21:]), ("short", good[:30])):
            with self.assertRaises(ValueError, msg=name): tgy.parse_tgy(bad)
        t = tgy.parse_tgy(good); lv = t["levels"][0]
        flipped = bytearray(good); flipped[lv["off"] + 5000] ^= 1
        with self.assertRaises(ValueError): tgy.level_bytes(bytes(flipped), lv)
        for field in ("layout", "mode", "pixfmt"):
            with self.assertRaises(ValueError): tgy.level_bytes(good, dict(lv, **{field: 7}))
    def test_index(self):
        ents = [dict(path="Assets/x/a.tgy", name="Alpha", size=4096 * 3, width=10, height=20), dict(path="Assets/x/b.tgy", name="Bêta é", size=4096 * 5, width=3000, height=2000)]
        th = [bytes([i]) * tgy.THUMB_BYTES for i in (1, 2)]; data = tgy.pack_index(ents, th); self.assertEqual(data[:4], b"TGIX")
        back = tgy.parse_index(data); self.assertEqual([e["path"] for e in back], [e["path"] for e in ents]); self.assertEqual(back[1]["name"], "Bêta é")
        self.assertEqual((back[1]["size"], back[1]["width"], back[1]["height"]), (4096 * 5, 3000, 2000)); self.assertEqual([e["thumb"] for e in back], th)
        a = struct.unpack_from("<I", data, 20)[0]; self.assertEqual(a % 4096, 0)                    # the atlas starts on a 4 KB boundary
        with self.assertRaises(ValueError): tgy.parse_index(b"TGIY" + data[4:])
        with self.assertRaises(ValueError): tgy.pack_index([dict(path="p" * 96, name="n", size=1, width=1, height=1)], [th[0]])
        self.assertEqual(len(tgy.parse_index(tgy.pack_index([], []))), 0)
    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "src"; out = Path(d) / "out"; src.mkdir()
            gradient(900, 600).save(src / "A one.jpg", quality=95); gradient(320, 240).save(src / "b.png"); gradient(64, 64).save(src / "b.jpg"); (src / "notes.txt").write_text("x"); (src / "broken.jpg").write_bytes(b"not a jpeg")
            import contextlib
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf): tgy_convert.main([str(src), str(out), "--card-dir", "Assets/tau_gallery/common/photos/"])
            ev = [json.loads(l) for l in buf.getvalue().splitlines()]
            self.assertEqual([e["event"] for e in ev].count("converted"), 3); self.assertEqual([e["event"] for e in ev].count("skip"), 1); self.assertEqual(ev[-1], {"event": "done", "pictures": 3})
            idx = tgy.parse_index((out / "tau-library.tdb").read_bytes()); self.assertEqual(len(idx), 3)
            names = sorted(p.name for p in out.iterdir()); self.assertFalse(any(n.endswith(".part") for n in names)); self.assertEqual(len([n for n in names if n.endswith(".tgy")]), 3)
            self.assertEqual(len({e["path"].lower() for e in idx}), 3)                                 # b.png and b.jpg (the same name) did not collide
            for e in idx:
                self.assertTrue(e["path"].startswith("Assets/tau_gallery/common/photos/") and not e["path"].startswith("/"))
                f = out / Path(e["path"]).name; self.assertEqual(f.stat().st_size, e["size"]); tgy.parse_tgy(f.read_bytes())
    def test_real_photo(self):
        p = ROOT.parent / "test-images/unsplash/unsplash-1441974231531-c6227db76b6e.jpg"
        if not p.exists(): self.skipTest("no test photo")
        img = Image.open(p); canvas, rect, thumb, size = tgy.render(img); x, y, w, h = rect
        self.assertEqual(size, (2560, 1705)); self.assertEqual((w, h), (800, 533)); self.assertEqual(len(tgy.pack_tgy(canvas, rect, thumb, size)), 4096 + 16384 + tgy.align(1728000))
        self.assertTrue(any(canvas[(y * 800 + 400) * 3 + k] for k in range(3)))                       # picture content, not black

def mutants():
    """Each edit of the library must make the suite fail."""
    src = (ROOT / "tools/gallery/tgy.py").read_text()
    muts = [("rgb order", "buf[data_off:data_off + data_len] = canvas", "buf[data_off:data_off + data_len] = canvas[::-1]"),
            ("stride", "CANVAS_W * 3, data_off", "CANVAS_W * 3 + 4, data_off"),
            ("565 order", "out[j] = v & 255; out[j + 1] = v >> 8", "out[j] = v >> 8; out[j + 1] = v & 255"),
            ("no centre", "return (cw - rw) // 2 // 2 * 2, (ch - rh) // 2, rw, rh", "return 0, 0, rw, rh"),
            ("enlarges", "if not upscale: s = min(s, 1.0)", "pass"),
            ("no crc check", 'if crc(data[:52]) != struct.unpack_from("<I", data, 52)[0]: raise ValueError("header checksum")', "pass"),
            ("no level crc", 'if crc(b) != lv["crc"]: raise ValueError("level checksum")', "pass"),
            ("align", "def align(n, a=ALIGN): return (n + a - 1) // a * a", "def align(n, a=ALIGN): return n"),
            ("atlas order", "buf[atlas_off + i * THUMB_BYTES:atlas_off + (i + 1) * THUMB_BYTES] = t", "buf[atlas_off + (n - 1 - i) * THUMB_BYTES:atlas_off + (n - i) * THUMB_BYTES] = t"),
            ("no orientation", "img = ImageOps.exif_transpose(img).convert", "img = img.convert")]
    import importlib, types
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("tgy"); exec(compile(src.replace(a, b), "tgy_mut", "exec"), mod.__dict__)
        sys.modules["tgy"] = mod; tgy_convert.tgy = mod; globals()["tgy"] = mod
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
        print(("killed  " if not r.wasSuccessful() else "SURVIVED"), name); bad += r.wasSuccessful()
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    unittest.main()
