#!/usr/bin/env python3
"""Tests of tools/gallery/tgy_recon.py (docs/TGY_RECONSTRUCTION_V0.md): the reconstruction must equal libjpeg-turbo's decode (Pillow) BIT FOR BIT for baseline and progressive JPEGs, 4:4:4, 4:2:0 and grey, odd and tiny sizes
and a real photo crop; and the whole chain JPEG coefficients -> 128 x 128 tiles -> TGY symbol layer v0 streams -> decoded tiles -> reassembled coefficients -> pixels must equal that decode too (lossless recompression with the
same pixels as a PC). `--mutants` edits the reconstruction; every edit must break the equality."""
import io, subprocess, sys, tempfile, types, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import jpegcoef, tgy_recon as R, tgy_v0 as V, test_jpegcoef as TJ
from PIL import Image

def ref_rgb(data): return np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))
def real_crop(w, h):
    p = next((ROOT.parent / "test-images/unsplash").glob("*c6227db76b6e.jpg")) if (ROOT.parent / "test-images/unsplash").is_dir() else None
    if p is None: return None
    d = Path(tempfile.mkdtemp()); out = d / "crop.jpg"
    subprocess.run(["/opt/homebrew/bin/jpegtran", "-crop", f"{w}x{h}+768+256", "-copy", "none", "-outfile", str(out), str(p)], check=True); return out.read_bytes()

class T(unittest.TestCase):
    def setUp(self):
        import signal
        def hang(s, f): raise TimeoutError("hung")
        signal.signal(signal.SIGALRM, hang); signal.alarm(300)
    def tearDown(self):
        import signal; signal.alarm(0)
    def same(self, data):
        j = jpegcoef.read_jpeg(data); mine = R.decode_pixels(j); self.assertTrue(np.array_equal(mine, ref_rgb(data)), "pixels differ from libjpeg-turbo"); return j
    def test_444_baseline_and_progressive(self):
        for w, h, kw in ((64, 48, dict(quality=90, subsampling=0)), (61, 43, dict(quality=85, subsampling=0, progressive=True)), (33, 17, dict(quality=60, subsampling=0))):
            self.same(TJ.encode(TJ.picture(w, h, seed=2), **kw))
    def test_420_baseline_and_progressive_odd_sizes(self):
        for w, h, kw in ((64, 48, dict(quality=90, subsampling=2)), (100, 77, dict(quality=85, subsampling=2)), (131, 97, dict(quality=70, subsampling=2, progressive=True)), (17, 5, dict(quality=90, subsampling=2)),
                         (33, 31, dict(quality=95, subsampling=2)), (2, 2, dict(quality=90, subsampling=2)), (3, 40, dict(quality=90, subsampling=2)), (200, 9, dict(quality=50, subsampling=2, progressive=True))):
            self.same(TJ.encode(TJ.picture(w, h, seed=3), **kw))
    def test_grey(self):
        self.same(TJ.encode(TJ.picture(50, 40).convert("L"), quality=88))
    def test_extreme_coefficients_and_saturation(self):                                                     # noise at quality 100 and 5: large coefficients, clipped samples, the range-limit wrap
        r = np.random.RandomState(7)
        for q in (100, 5):
            img = Image.fromarray(r.randint(0, 256, (48, 64, 3)).astype(np.uint8)); self.same(TJ.encode(img, quality=q, subsampling=2)); self.same(TJ.encode(img, quality=q, subsampling=0))
    def test_real_photo_crop(self):
        data = real_crop(512, 384)
        if data is None: self.skipTest("no test photo")
        self.same(data)
    def test_end_to_end_through_the_symbol_layer(self):
        cases = [TJ.encode(TJ.picture(300, 200, seed=4), quality=85, subsampling=2, progressive=True), TJ.encode(TJ.picture(257, 129, seed=5), quality=90, subsampling=0)]
        crop = real_crop(384, 384)
        if crop: cases.append(crop)
        for data in cases:
            j = jpegcoef.read_jpeg(data); ref = ref_rgb(data); tiles = R.split_tiles(j); self.assertGreater(len(tiles), 1); out = []
            for ty, tx, grids, shapes in tiles:
                stream = V.encode_tile(grids, shapes); out.append((ty, tx, V.decode_tile(stream, shapes), shapes))
            k = jpegcoef.read_jpeg(data); R.assemble(k, out)
            for a, b in zip(j.components, k.components): self.assertTrue(np.array_equal(a.coef, b.coef))      # lossless
            self.assertTrue(np.array_equal(R.decode_pixels(k), ref))                                         # and the same pixels as libjpeg-turbo
    def test_partial_tiles_have_the_blocks_that_exist(self):
        j = jpegcoef.read_jpeg(TJ.encode(TJ.picture(300, 200, seed=4), quality=85, subsampling=2)); tiles = R.split_tiles(j)
        self.assertEqual((max(t[0] for t in tiles) + 1, max(t[1] for t in tiles) + 1), (2, 3)); last = tiles[-1]
        self.assertEqual(last[3], [(((200 + 15) // 16 * 2) - 16, ((300 + 15) // 16 * 2) - 32), (((200 + 15) // 16) - 8, ((300 + 15) // 16) - 16), (((200 + 15) // 16) - 8, ((300 + 15) // 16) - 16)])
        self.assertEqual(tiles[0][3], [(16, 16), (8, 8), (8, 8)])
    def test_tile_profile_differs_from_libjpeg_only_on_the_tile_edge_ring(self):
        data = TJ.encode(TJ.picture(300, 270, seed=8), quality=85, subsampling=2, progressive=True); j = jpegcoef.read_jpeg(data); a = R.decode_pixels(j).astype(int); b = R.decode_pixels_tgy(j).astype(int)
        ys, xs = np.nonzero(np.abs(a - b).max(axis=2)); self.assertGreater(len(ys), 0)                                  # the seams do differ
        self.assertTrue(all((y % 128 in (0, 127)) or (x % 128 in (0, 127)) for y, x in zip(ys, xs)))                  # and only in the outermost row and column of each tile
        inner = np.ones(a.shape[:2], bool); inner[::128] = False; inner[127::128] = False; inner[:, ::128] = False; inner[:, 127::128] = False
        self.assertTrue(np.array_equal(a[inner], b[inner]))
    def test_single_tile_pictures_are_exact_in_the_tile_profile(self):
        for w, h in ((128, 128), (100, 77), (17, 5), (2, 2)):
            data = TJ.encode(TJ.picture(w, h, seed=9), quality=88, subsampling=2); j = jpegcoef.read_jpeg(data); self.assertTrue(np.array_equal(R.decode_pixels_tgy(j), ref_rgb(data)))
    def test_primitives(self):
        z = np.zeros((1, 8, 8), dtype=np.int64); self.assertTrue((R.idct_islow(z) == 128).all()); dc = z.copy(); dc[0, 0, 0] = 80; self.assertTrue((R.idct_islow(dc) == 138).all())     # DC 80 / 8 = 10 above the level shift
        big = z.copy(); big[0, 0, 0] = 100000; self.assertTrue((R.idct_islow(big) == 255).all()); neg = z.copy(); neg[0, 0, 0] = -100000; self.assertTrue((R.idct_islow(neg) == 0).all())
        self.assertEqual(R.ycc_to_rgb(np.array([128]), np.array([128]), np.array([128])).tolist(), [[128, 128, 128]]); self.assertEqual(R.ycc_to_rgb(np.array([255]), np.array([128]), np.array([128])).tolist(), [[255, 255, 255]]); self.assertEqual(R.ycc_to_rgb(np.array([0]), np.array([128]), np.array([128])).tolist(), [[0, 0, 0]])
        flat = np.full((4, 5), 77, dtype=np.uint8); self.assertTrue((R.upsample_h2v2_fancy(flat) == 77).all()); self.assertEqual(R.upsample_h2v2_fancy(np.zeros((3, 2), np.uint8)).shape, (6, 4))

def mutants():
    src = (ROOT / "tools/gallery/tgy_recon.py").read_text()
    muts = [("f0541", "f0541=4433", "f0541=4434"), ("f1175", "f1175=9633", "f1175=9632"), ("f2562", "f2562=20995", "f2562=20994"), ("pass1 shift", "_descale(col[r], CONST_BITS - PASS1_BITS)", "_descale(col[r], CONST_BITS - PASS1_BITS + 1)"),
            ("pass2 shift", "CONST_BITS + PASS1_BITS + 3", "CONST_BITS + PASS1_BITS + 2"), ("rounding", "return (x + (1 << (n - 1))) >> n", "return x >> n"), ("range limit", "np.where(j < 512, 255, 0)" if False else "np.where(j < 512, 255, np.where(j < 896, 0, j - 896))", "np.where(j < 512, 255, np.where(j < 896, 0, 0))"),
            ("odd part", "a0 = a0 + z1 + z3; a1", "a0 = a0 + z1; a1"), ("fix cr_r", "(fix(1.40200) * x + ONE_HALF) >> SCALE", "(fix(1.4) * x + ONE_HALF) >> SCALE"), ("g table", "(-fix(0.34414)) * x + ONE_HALF", "(-fix(0.34414)) * x"),
            ("g shift", "((CB_G[cb] + CR_G[cr]) >> 16)", "((CB_G[cb] + CR_G[cr]) >> 15)"), ("fancy first col", "(s[:, 0] * 4 + 8) >> 4", "(s[:, 0] * 4 + 7) >> 4"), ("fancy last col", "(s[:, -1] * 4 + 7) >> 4", "(s[:, -1] * 4 + 8) >> 4"),
            ("fancy mid", "(s[:, 1:-1] * 3 + s[:, 2:] + 7) >> 4", "(s[:, 1:-1] * 3 + s[:, 2:] + 8) >> 4"), ("fancy up", "up = np.vstack([p[:1], p[:-1]])", "up = np.vstack([p[1:2], p[:-1]])"),
            ("fancy down", "down = np.vstack([p[1:], p[-1:]])", "down = np.vstack([p[1:], p[-2:-1]])"), ("narrow plane", "if w <= 2:", "if w <= 1:"), ("crop", "plane[:true_h_samples, :true_w_samples]", "plane[:true_h_samples + 0, :true_w_samples - 1 if true_w_samples > 40 else true_w_samples]")]
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("tgy_recon"); sys.modules["tgy_recon"] = mod
        try: exec(compile(src.replace(a, b, 1), "tgy_recon_mut", "exec"), mod.__dict__)
        except Exception: print("killed   (does not run)", name); continue
        globals()["R"] = mod
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
        print(("killed  " if not r.wasSuccessful() else "SURVIVED"), name); bad += r.wasSuccessful()
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    unittest.main()
