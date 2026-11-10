#!/usr/bin/env python3
"""Tests of the FROZEN TGY symbol layer v0 (tools/gallery/tgy_v0.py, docs/TGY_SYMBOL_LAYER_V0.md): the conformance vectors (every stream decodes to its blocks and every set of blocks encodes to its stream), byte equality with the
exploratory model it was frozen from, the constants of the spec, and `--mutants`: every edit of the frozen module that changes a stream bit must be caught."""
import base64, io, json, random, struct, sys, types, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery")); sys.path.insert(0, str(ROOT / "sim"))
import tgy_v0 as V, tgymodel as M, test_tgymodel as T, make_tgy_v0_vectors as G
VEC = json.loads((ROOT / "docs/conformance/tgy_v0_vectors.json").read_text())

class Q(unittest.TestCase):
    def setUp(self):
        import signal
        def hang(s, f): raise TimeoutError("hung")
        signal.signal(signal.SIGALRM, hang); signal.alarm(60)
    def tearDown(self):
        import signal; signal.alarm(0)
    def test_vectors(self):
        self.assertGreaterEqual(len(VEC["vectors"]), 9)
        for v in VEC["vectors"]:
            shapes = [tuple(s) for s in v["shapes"]]; grids = G.unpack(v["blocks"], shapes)
            self.assertEqual(V.encode_tile(grids, shapes).hex(), v["stream"], v["name"])
            self.assertEqual(V.decode_tile(bytes.fromhex(v["stream"]), shapes), grids, v["name"])
    def test_vector_constants(self):
        self.assertEqual((VEC["version"], VEC["contexts"], VEC["limit"]), (0, 1250, 30)); self.assertEqual(VEC["rate_table"], V.REC); self.assertEqual(VEC["position_groups"], V.KG)
        self.assertEqual([tuple(x) for x in VEC["neighbour_table"]], V.LN)
    def test_synthetic_vectors_reproduce(self):
        have = {v["name"]: v for v in VEC["vectors"]}
        for name, grids, shapes in G.synthetic():
            self.assertEqual(have[name]["stream"], V.encode_tile(grids, shapes).hex(), name); self.assertEqual(have[name]["blocks"], G.pack(grids), name)
    def test_equals_the_exploratory_model(self):
        for seed in range(6):
            grids, shapes = T.synth_tile(70 + seed, rows=(5, 3), cols=(5, 3)); e = M.EncIO(M.Engine("count")); M.code_tile(e, grids, shapes, False, dict(local=True))
            self.assertEqual(V.encode_tile(grids, shapes), e.finish(), seed)
    def test_spec_constants(self):
        self.assertEqual(V.REC[0], 32768); self.assertEqual(V.REC[1], 26214); self.assertEqual(V.REC[2], 18724); self.assertEqual(V.REC[30], 2080); self.assertEqual(len(V.REC), 31)
        self.assertEqual((V.O_D0, V.O_DS, V.O_DX, V.O_DM, V.O_LT, V.O_SG, V.O_G1, V.O_MX, V.O_MM), (0, 2, 4, 24, 44, 170, 300, 430, 1230)); self.assertEqual(V.O_MM + 20, V.N_CTX)
        self.assertEqual(V.O_DM + 20, V.O_LT); self.assertEqual(V.O_LT + 126, V.O_SG); self.assertEqual(V.O_SG + 130, V.O_G1); self.assertEqual(V.O_G1 + 130, V.O_MX); self.assertEqual(V.O_MX + 800, V.O_MM)
        self.assertEqual(V.KG[1], 0); self.assertEqual(V.KG[2], 1); self.assertEqual(V.KG[63], 12); self.assertEqual(V.LN[1], (0, -1)); self.assertEqual(V.LN[2], (-1, 0)); self.assertEqual(V.LN[0], (-1, -1))
    def test_every_context_index_is_in_range_and_used(self):
        used = set(); real = V.EncIO.bit
        def spy(self, c, b): used.add(c); assert 0 <= c < V.N_CTX; return real(self, c, b)
        V.EncIO.bit = spy
        try:
            for v in VEC["vectors"]:
                shapes = [tuple(s) for s in v["shapes"]]; V.encode_tile(G.unpack(v["blocks"], shapes), shapes)
        finally: V.EncIO.bit = real
        self.assertGreater(len(used), 400)                                                           # the vectors exercise a broad set of contexts
    def test_the_spec_text_matches_the_module(self):                                                   # the numbers printed in docs/TGY_SYMBOL_LAYER_V0.md cannot drift from the reference
        import re
        doc = (ROOT / "docs/TGY_SYMBOL_LAYER_V0.md").read_text()
        self.assertEqual([int(x) for x in re.search(r"for n = 0 \.\. 30: ([0-9, ]+) \(the vectors", doc).group(1).split(",")], V.REC)
        self.assertIn("N_CTX = 1,250", doc); self.assertIn("LIMIT = 30", doc); self.assertIn("(2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49)", doc); self.assertIn("21 plausible one-line deviations", doc)
    def test_engine(self):
        e = V.Engine(); e.update(0, 0); self.assertEqual(e.p[0], 16384 + ((16384 * 32768) >> 16)); self.assertEqual(e.n[0], 1)
        for _ in range(200): e.update(1, 0)
        self.assertEqual(e.p[1], 32768 - 32); self.assertEqual(e.n[1], 30)
        for _ in range(200): e.update(2, 1)
        self.assertEqual(e.p[2], 32)

def mutants():
    src = (ROOT / "tools/gallery/tgy_v0.py").read_text()
    muts = [("limit", "LIMIT = 30 ", "LIMIT = 31 "), ("rate", "int(65536 / (n + 1.5))", "int(65536 / (n + 1.6))"), ("rate cap", "min(32768, int", "min(32767, int"), ("clamp", "min(32768 - 32, max(32, p))", "min(32768 - 33, max(32, p))"),
            ("kg", "(2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 49)", "(2, 3, 4, 5, 7, 9, 12, 16, 21, 28, 37, 50)"), ("lb", "else (3 if x <= 4 else 4)", "else (3 if x <= 5 else 4)"),
            ("dc pred", "pred = left[0] if left is not None else (above[0] if above is not None else 0)", "pred = above[0] if above is not None else (left[0] if left is not None else 0)"),
            ("sign ctx", "neg = io.bit(O_DS + cc,", "neg = io.bit(O_DS,"), ("tree ctx", "O_LT + cc * 63 + node - 1", "O_LT + node - 1"), ("tree order", "for i in range(5, -1, -1): node", "for i in range(0, 6): node"),
            ("sig ctx", "O_SG + (cc * 13 + KG[k]) * 5 + lc", "O_SG + (cc * 13 + KG[k]) * 5"), ("g1 ctx", "O_G1 + (cc * 13 + KG[k]) * 5 + lc", "O_G1 + (cc * 13 + KG[k]) * 5"),
            ("local sum", "lc = _lb(abs(out[li]) + abs(out[ui]))", "lc = _lb(abs(out[li]))"), ("implicit last", "if k < last and not io.bit(O_SG", "if not io.bit(O_SG"),
            ("exp cap mag", "((cc * 8 + kg) * 5 + lc) * 10) + min(i, 9)", "((cc * 8 + kg) * 5 + lc) * 10) + min(i, 8)"), ("exp cap dc", "O_DX + cc * 10 + min(i, 9)", "O_DX + cc * 10 + min(i, 8)"),
            ("mant ctx", "io.bit(O_MM + cc * 10 + min(e, 9), bit)", "io.bit(O_MM + min(e, 9), bit)"), ("dc mant", "io.bit(O_DM + cc * 10 + min(e, 9), bit)", "io.bypass(bit)"),
            ("mag kg cap", "kg = min(KG[k], 7)", "kg = min(KG[k], 6)"), ("sign bypass", "neg = io.bypass((1 if blk[k] < 0 else 0) if enc else None); out[k]", "neg = io.bit(O_MM, (1 if blk[k] < 0 else 0) if enc else None); out[k]"),
            ("component class", "cc = 0 if ci == 0 else 1;", "cc = 0;")]
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("tgy_v0"); sys.modules["tgy_v0"] = mod
        try: exec(compile(src.replace(a, b, 1), "tgy_v0_mut", "exec"), mod.__dict__)
        except Exception: print("killed   (does not run)", name); continue
        global V; V = mod; globals()["V"] = mod
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(Q))
        print(("killed  " if not r.wasSuccessful() else "SURVIVED"), name); bad += r.wasSuccessful()
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    unittest.main()
