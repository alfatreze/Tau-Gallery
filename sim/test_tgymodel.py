#!/usr/bin/env python3
"""Tests of tools/gallery/tgymodel.py, the TGY symbol layer of the measurement (docs/M3_STREAMING_NOTE.md section 11): every variant (fixed or count-based engine, with and without neighbour contexts, with trained priors)
must round-trip tiles bit for bit through the real TVC encoder and decoder, the ideal cost must agree with the real stream size, and the engine and binarisation edge cases must hold."""
import io, random, sys, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgymodel as M, jpegcoef, test_jpegcoef as TJ

def synth_tile(seed, rows=(4, 2), cols=(4, 2)):
    r = random.Random(seed); grids = []
    for gr, gc in zip(rows, cols):
        g = []
        for y in range(gr):
            row = []
            for x in range(gc):
                b = [0] * 64; b[0] = r.randint(-1500, 1500) if r.random() < 0.9 else r.choice([0, 2047, -2047])
                last = r.choice([0, 0, 1, 5, 20, 63]) if r.random() < 0.7 else r.randint(0, 63)
                for k in range(1, last + 1):
                    if k == last or r.random() < 0.4: b[k] = r.choice([-1, 1, 2, -2, 3, -7, 15, -40, 300, -1023]) if r.random() < 0.9 else r.randint(-1023, 1023) or 1
                row.append(b)
            g.append(row)
        grids.append(g)
    return grids, list(zip(rows, cols))
def run(grids, shapes, mode, nbr, priors=None, opt=None):
    e = M.EncIO(M.Engine(mode, priors)); M.code_tile(e, grids, shapes, nbr, opt); data = e.finish()
    back = M.code_tile(M.DecIO(M.Engine(mode, priors), data), None, shapes, nbr, opt); c = M.CostIO(M.Engine(mode, priors)); M.code_tile(c, grids, shapes, nbr, opt)
    return data, back, c

class T(unittest.TestCase):
    def test_roundtrip_all_variants(self):
        for seed in range(6):
            grids, shapes = synth_tile(seed)
            for mode in ("fixed", "count"):
                for nbr in (False, True):
                    data, back, c = run(grids, shapes, mode, nbr); self.assertEqual(back, grids, (seed, mode, nbr))
    def test_priors_roundtrip_and_help(self):
        grids, shapes = synth_tile(11); c = M.CostIO(M.Engine("count")); M.code_tile(c, grids, shapes, True); pri = M.train_priors(c.counts)
        data, back, c2 = run(grids, shapes, "count", True, pri); self.assertEqual(back, grids); self.assertLess(c2.bits, c.bits)         # priors trained on the same data cost less
    def test_cost_matches_the_real_stream(self):
        for seed in (1, 2, 3):
            grids, shapes = synth_tile(seed, rows=(8, 4), cols=(8, 4))
            for mode in ("fixed", "count"):
                data, back, c = run(grids, shapes, mode, True); self.assertEqual(back, grids)
                self.assertLess(abs(len(data) * 8 - c.bits), 48 + 0.01 * c.bits, (seed, mode, len(data) * 8, c.bits))               # the 5-byte flush and rounding
    def test_extremes(self):
        z = [0] * 64; big = [2047] + [1023 if k % 2 else -1023 for k in range(1, 64)]; neg = [-2047] + [-1] * 63
        grids = [[[z, big], [neg, z]]]; shapes = [(2, 2)]
        for mode in ("fixed", "count"):
            for nbr in (False, True): self.assertEqual(run(grids, shapes, mode, nbr)[1], grids)
    def test_all_zero_tile_is_tiny(self):
        z = [0] * 64; grids = [[[z] * 8 for _ in range(8)]]; data, back, c = run(grids, [(8, 8)], "count", True); self.assertEqual(back, grids)
        self.assertLess(c.bits, 120)                                                                                                    # 64 blocks of nothing cost a few dozen bits once the contexts have learned
    def test_real_jpeg_tiles(self):
        img = TJ.picture(256, 256, seed=5); j = jpegcoef.read_jpeg(TJ.encode(img, quality=88, subsampling=2, progressive=True)); n = 0
        for grids, shapes in M.tiles_of(j):
            self.assertEqual(shapes, [(16, 16), (8, 8), (8, 8)]); n += 1
            for mode, nbr in (("fixed", False), ("count", True)): self.assertEqual(run(grids, shapes, mode, nbr)[1], grids)
        self.assertEqual(n, 4)
    OPTS = [dict(zip(("nz", "local", "dcmed", "nznb"), c)) for c in __import__("itertools").product([False, True], repeat=4)]
    def test_rich_options_roundtrip(self):
        for seed in range(5):
            grids, shapes = synth_tile(40 + seed, rows=(5, 3), cols=(5, 3))
            for opt in self.OPTS: self.assertEqual(run(grids, shapes, "count", False, None, opt)[1], grids, (seed, opt))
    def test_rich_options_extremes_and_real_tiles(self):
        z = [0] * 64; full = [5] + [1] * 63; big = [2047] + [1023 if k % 2 else -1023 for k in range(1, 64)]; one = [0] * 63 + [-1]
        grids = [[[z, full, big], [one, z, full], [big, one, z]]]
        for opt in self.OPTS: self.assertEqual(run(grids, [(3, 3)], "count", False, None, opt)[1], grids, opt)                          # all-nonzero blocks use the implicit flags, the lone last coefficient the longest zero run
        img = TJ.picture(256, 256, seed=9); j = jpegcoef.read_jpeg(TJ.encode(img, quality=85, subsampling=2, progressive=True))
        for g, sh in M.tiles_of(j):
            for opt in self.OPTS: self.assertEqual(run(g, sh, "count", False, None, opt)[1], g, opt)
    def test_rich_cost_matches_the_real_stream(self):
        grids, shapes = synth_tile(55, rows=(8, 4), cols=(8, 4))
        for opt in self.OPTS:
            data, back, c = run(grids, shapes, "count", False, None, opt); self.assertEqual(back, grids); self.assertLess(abs(len(data) * 8 - c.bits), 48 + 0.01 * c.bits, opt)
    def test_rich_tables(self):
        self.assertEqual(M.ZZI[M.ZZN[17]], 17); self.assertTrue(all(M.ZZI[M.ZZN[k]] == k for k in range(64)))
        for k in range(1, 64):
            for q in M.LN[k]: self.assertTrue(q < k)                                                                                       # the left and upper coefficients are always coded earlier
        self.assertEqual(M.LN[1], (0, -1)); self.assertEqual(M.LN[2], (-1, 0)); self.assertEqual([M._remb(r) for r in (0, 1, 4, 5, 6, 7, 9, 10, 14, 15, 22, 23, 63)], [0, 1, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8])
        self.assertEqual(M._med(3, 9, 5), 5); self.assertEqual(M._med(3, 9, 20), 9); self.assertEqual(M._med(3, 9, -4), 3)
    def test_engine(self):
        e = M.Engine("fixed"); self.assertEqual(e.prob("a"), 16384); e.update("a", 1); self.assertEqual(e.p["a"], 16384 - (16384 >> 5)); e.update("a", 0); self.assertGreater(e.p["a"], 16384 - 512)
        e = M.Engine("count"); e.prob("c"); e.update("c", 0); self.assertEqual(e.p["c"], 16384 + (((32768 - 16384) * M.REC[0]) >> 16))      # the first update moves half way (the rate starts at 1/2)
        for _ in range(500): e.update("c", 0)
        self.assertLessEqual(e.p["c"], 32768 - 32); self.assertGreater(e.p["c"], 32768 - 80)
        for _ in range(500): e.update("c", 1)
        self.assertGreaterEqual(e.p["c"], 32); self.assertLess(e.p["c"], 80)
        self.assertEqual(M.REC[0], 32768); self.assertEqual(M.REC[1], int(65536 / 2.5)); self.assertEqual(M.REC[M.LIMIT], int(65536 / (M.LIMIT + 1.5)))
        pe = M.Engine("count", {"x": (30000, 6)}); self.assertEqual(pe.prob("x"), 30000); self.assertEqual(pe.n["x"], 6)
    def test_groups_cover(self):
        self.assertEqual(M.KG[1], 0); self.assertEqual(M.KG[63], 12); self.assertEqual(sorted(set(M.KG[1:])), list(range(13))); self.assertTrue(all(M.KG[k] <= M.KG[k + 1] for k in range(1, 63)))
    def test_neighbour_contexts_change_the_stream(self):
        grids, shapes = synth_tile(21, rows=(6, 3), cols=(6, 3)); a = run(grids, shapes, "fixed", False)[0]; b = run(grids, shapes, "fixed", True)[0]; self.assertNotEqual(a, b)
    def test_sensitivity(self):
        grids, shapes = synth_tile(31); a = run(grids, shapes, "count", True)[0]
        grids[0][1][1][7] += 1; b = run(grids, shapes, "count", True)[0]; self.assertNotEqual(a, b)

if __name__ == "__main__":
    unittest.main()
