#!/usr/bin/env python3
"""Tests of tools/gallery/tvcrc.py, the TVC range coder reference (docs/M3_STREAMING_NOTE.md): round trips of context and bypass bins, probability bounds, carry propagation through runs of 0xFF, zero padding past the end,
the exact first bytes of fixed sequences (regression vectors that sim/cross_tvcrc.py also compares with the Rust implementation), and `--mutants`: every edit of the module must make the suite fail."""
import io, random, sys, types, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tvcrc

def seq(seed, n, nctx=16, bypass=0.2, bias=None):
    r = random.Random(seed); ps = [r.choice([0.01, 0.05, 0.3, 0.5, 0.7, 0.95, 0.99]) for _ in range(nctx)] if bias is None else bias
    out = []
    for _ in range(n):
        if r.random() < bypass: out.append(("b", None, int(r.random() < 0.5)))
        else: c = r.randrange(nctx); out.append(("c", c, int(r.random() < ps[c])))
    return out
def encode(s, nctx=16):
    e = tvcrc.Encoder(); p = tvcrc.new_probs(nctx)
    for k, c, b in s:
        if k == "c": e.bit(p, c, b)
        else: e.bypass(b)
    return e.finish(), p
def decode(data, s, nctx=16):
    d = tvcrc.Decoder(data); p = tvcrc.new_probs(nctx); out = []
    for k, c, _ in s: out.append(d.bit(p, c) if k == "c" else d.bypass())
    return out, p

def carry_seq(seed):
    """3,000 mixed bins on 2 even contexts: a long adaptive run on which the encoder propagates carries (found by search, 0.3 bypass)."""
    r = random.Random(seed); out = []
    for _ in range(3000):
        if r.random() < 0.3: out.append(("b", None, int(r.random() < 0.5)))
        else: out.append(("c", r.randrange(2), int(r.random() < 0.5)))
    return out
def carry_run_count(s):
    """How many carries the (reference) encoder propagated through two or more pending bytes on this sequence."""
    class E(tvcrc.Encoder):
        n = 0
        def _shift_low(self):
            if (self.low >> 32) != 0 and self.cache_size > 1: E.n += 1
            super()._shift_low()
    e = E(); p = tvcrc.new_probs(2)
    for k, c, b in s:
        if k == "c": e.bit(p, c, b)
        else: e.bypass(b)
    e.finish(); return E.n

class T(unittest.TestCase):
    def setUp(self):                                                                                  # a coder edited to hang (a zero range never normalises) fails the test after 5 s instead of hanging the suite
        import signal
        def hang(signum, frame): raise TimeoutError("hung")
        signal.signal(signal.SIGALRM, hang); signal.alarm(5)
    def tearDown(self):
        import signal; signal.alarm(0)
    def test_roundtrip(self):
        for seed, n in ((1, 1), (2, 50), (3, 1000), (4, 20000)):
            s = seq(seed, n); data, pe = encode(s); bits, pd = decode(data, s)
            self.assertEqual(bits, [b for _, _, b in s]); self.assertEqual(pe, pd)               # the decoder's contexts end where the encoder's did
    def test_only_bypass_and_only_context(self):
        s = seq(5, 3000, bypass=1.0); self.assertEqual(decode(encode(s)[0], s)[0], [b for _, _, b in s])
        s = seq(6, 3000, bypass=0.0); self.assertEqual(decode(encode(s)[0], s)[0], [b for _, _, b in s])
    def test_empty(self):
        data, _ = encode([]); self.assertEqual(len(data), 5)                                          # the 5-byte flush of a fresh coder
        self.assertEqual(data, bytes(5))
    def test_probability_bounds(self):
        p = tvcrc.PROB_INIT
        for _ in range(5000): p = tvcrc.update(p, 0)
        self.assertLess(p, tvcrc.PROB_ONE); self.assertGreater(p, tvcrc.PROB_ONE - 40)
        for _ in range(5000): p = tvcrc.update(p, 1)
        self.assertGreater(p, 0); self.assertLess(p, 40)
        self.assertEqual(tvcrc.update(16384, 0), 16384 + (16384 >> 5)); self.assertEqual(tvcrc.update(16384, 1), 16384 - (16384 >> 5))
    def test_extreme_skew_roundtrips(self):
        for bias in ([0.0] * 4, [1.0] * 4, [0.0, 1.0, 0.0, 1.0]):
            s = seq(7, 8000, nctx=4, bypass=0.0, bias=bias); self.assertEqual(decode(encode(s, 4)[0], s, 4)[0], [b for _, _, b in s])
    def test_carry_runs(self):
        runs = 0
        for seed in range(40):                                                                       # sequences on which the encoder propagates a carry through pending 0xFF bytes
            s = carry_seq(seed); e = tvcrc.Encoder(); p = tvcrc.new_probs(2)
            for k, c, b in s:
                if k == "c":
                    e.bit(p, c, b)
                else:
                    e.bypass(b)
            data = e.finish()
            if (tvcrc.Encoder is not None) and carry_run_count(s) > 0: runs += 1
            self.assertEqual(decode(data, s, 2)[0], [b for _, _, b in s])
        self.assertGreater(runs, 0)
    def test_zero_padding(self):
        s = seq(8, 500); data, _ = encode(s)
        for cut in (1, 2, 3):                                                                         # the flush may end in zero bytes: dropping trailing zeros must not change the decode
            trimmed = data
            while cut and trimmed.endswith(b"\x00"): trimmed = trimmed[:-1]; cut -= 1
            self.assertEqual(decode(trimmed, s)[0], [b for _, _, b in s])
        self.assertEqual(decode(b"", [("c", 0, 0)] * 8, 1)[0], [0] * 8)                              # no data at all: all zeros decode as zero bins
    def test_missing_bytes_read_as_zero(self):
        s = seq(9, 400)                                                                               # a short stream decodes as if padded with explicit zero bytes
        for short in (b"", b"\x00", b"\x12\x34"):
            self.assertEqual(decode(short, s)[0], decode(short + bytes(2000), s)[0])
    def test_shift_low_carry_with_high_low_bits(self):
        e = tvcrc.Encoder(); e.low = (1 << 32) | 0xFF800000; e.cache = 0x12; e.cache_size = 2                  # a carry while the low 32 bits are already 0xFF......: the pending bytes must still be flushed, incremented
        e._shift_low(); self.assertEqual(bytes(e.out), bytes([0x13, 0x00])); self.assertEqual(e.cache, 0xFF); self.assertEqual(e.cache_size, 1); self.assertEqual(e.low, 0x80000000 & 0xFFFFFFFF if False else (0x00800000 << 8))
        e = tvcrc.Encoder(); e.low = 0xFF800000; e.cache = 0x12; e.cache_size = 2                              # no carry and 0xFF in the top byte: the byte stays pending
        e._shift_low(); self.assertEqual(bytes(e.out), b""); self.assertEqual(e.cache_size, 3)
    def test_vectors(self):
        self.assertEqual(encode([("c", 0, 1), ("c", 0, 0), ("c", 1, 1), ("b", None, 1), ("b", None, 0)] * 20, 2)[0].hex(), VEC_A)
        self.assertEqual(encode(seq(11, 300))[0].hex(), VEC_B)

VEC_A = "00b3f0b413d229373c2652052ca02d6f"
VEC_B = "00e70a29f29402e0f0a9d30a2b8d298b413cf81da7d90d0fedee1b5e3c64db9c7ec118c42e41"

def mutants():
    src = (ROOT / "tools/gallery/tvcrc.py").read_text()
    muts = [("init", "PROB_INIT, PROB_ONE, SHIFT = 16384, 32768, 5", "PROB_INIT, PROB_ONE, SHIFT = 16384, 32768, 4"), ("prob init", "PROB_INIT, PROB_ONE, SHIFT = 16384", "PROB_INIT, PROB_ONE, SHIFT = 16385"),
            ("update", "return p - (p >> SHIFT) if bit else p + ((PROB_ONE - p) >> SHIFT)", "return p + (p >> SHIFT) if bit else p - ((PROB_ONE - p) >> SHIFT)"),
            ("bound shift", "bound = (self.range >> 15) * p\n        if bit:", "bound = (self.range >> 14) * p\n        if bit:"),
            ("carry", "if (self.low & M32) < 0xFF000000 or (self.low >> 32) != 0:", "if (self.low & M32) < 0xFF000000:"),
            ("carry add", "self.out.append((temp + carry) & 0xFF)", "self.out.append(temp)"),
            ("flush", "for _ in range(5): self._shift_low()", "for _ in range(4): self._shift_low()"),
            ("normalize", "while self.range < (1 << 24):\n            self.range = (self.range << 8) & M32; self._shift_low()", "while self.range < (1 << 23):\n            self.range = (self.range << 8) & M32; self._shift_low()"),
            ("bypass", "self.range >>= 1\n        if bit: self.low += self.range", "self.range >>= 1\n        if bit: self.low += self.range + 1"),
            ("dec compare", "if self.code >= bound: self.code -= bound", "if self.code > bound: self.code -= bound"),
            ("dec init", "for _ in range(5): self.code = ((self.code << 8) | self._byte()) & M32", "for _ in range(4): self.code = ((self.code << 8) | self._byte()) & M32"),
            ("pad", "return 0\n    def _normalize(self):\n        while self.range < (1 << 24):\n            self.range = (self.range << 8) & M32; self.code", "return 1\n    def _normalize(self):\n        while self.range < (1 << 24):\n            self.range = (self.range << 8) & M32; self.code"),
            ("dec bypass", "if self.code >= self.range: self.code -= self.range; b = 1", "if self.code > self.range: self.code -= self.range; b = 1")]
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("tvcrc"); exec(compile(src.replace(a, b, 1), "tvcrc_mut", "exec"), mod.__dict__); sys.modules["tvcrc"] = mod; globals()["tvcrc"] = mod
        import signal
        def hang(signum, frame): raise TimeoutError
        signal.signal(signal.SIGALRM, hang); signal.alarm(20)                                          # a mutant that hangs the coder (a zero range never normalises) is a killed mutant
        try:
            r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T)); ok = r.wasSuccessful()
        except TimeoutError:
            ok = False
        finally:
            signal.alarm(0)
        print(("killed  " if not ok else "SURVIVED"), name); bad += ok
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    if "--print-vectors" in sys.argv:
        print(encode([("c", 0, 1), ("c", 0, 0), ("c", 1, 1), ("b", None, 1), ("b", None, 0)] * 20, 2)[0].hex()); print(encode(seq(11, 300))[0].hex()); sys.exit(0)
    unittest.main()
