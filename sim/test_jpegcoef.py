#!/usr/bin/env python3
"""Tests of tools/gallery/jpegcoef.py against Pillow (libjpeg-turbo): the coefficient reader's output, dequantised and inverse transformed, must reproduce the pixels Pillow decodes (luma within rounding, chroma at the
subsampled grid), for baseline and progressive, 4:4:4 and 4:2:0, with and without restart intervals, odd sizes; the DC coefficients must equal Pillow's 1/8-scale decode; damaged and unsupported files must raise. `--mutants`
edits the reader; every edit must make the suite fail."""
import io, sys, types, unittest
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import jpegcoef
from PIL import Image

def picture(w, h, seed=1, patch=True):
    r = np.random.RandomState(seed); y, x = np.mgrid[0:h, 0:w]
    img = np.stack([(x * 255 // max(1, w - 1)), (y * 255 // max(1, h - 1)), ((x + y) * 255 // max(1, w + h - 2))], axis=-1).astype(np.float64)
    img += r.normal(0, 6, img.shape)
    if patch: img[h // 3:h // 2, w // 4:w // 2] = r.randint(0, 256, (h // 2 - h // 3, w // 2 - w // 4, 3))     # a noisy patch: many coefficients, long runs and EOB runs
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
def encode(img, **kw):
    b = io.BytesIO(); img.save(b, "JPEG", **kw); return b.getvalue()
def rgb_to_ycc(rgb):
    r, g, b = [rgb[..., i].astype(np.float64) for i in range(3)]
    return 0.299 * r + 0.587 * g + 0.114 * b, -0.168736 * r - 0.331264 * g + 0.5 * b + 128, 0.5 * r - 0.418688 * g - 0.081312 * b + 128

class T(unittest.TestCase):
    def check(self, w, h, patch=True, **kw):
        img = picture(w, h, patch=patch); data = encode(img, **kw); j = jpegcoef.read_jpeg(data)
        self.assertEqual((j.width, j.height), (w, h)); self.assertEqual(j.progressive, bool(kw.get("progressive")))
        ref = np.asarray(Image.open(io.BytesIO(data)).convert("RGB")); ry, rcb, rcr = rgb_to_ycc(ref)
        y = jpegcoef.idct_plane(j.components[0], j.quant[j.components[0].tq])[:h, :w]
        err = np.abs(y - ry); self.assertLess(err.mean(), 1.0, kw); self.assertLess(err.max(), 8.0, kw)                                   # luma: the same picture up to rounding and colour-conversion noise
        for comp, refc in zip(j.components[1:], (rcb, rcr)):
            plane = jpegcoef.idct_plane(comp, j.quant[comp.tq]); fy, fx = j.components[0].v // comp.v, j.components[0].h // comp.h
            ch, cw = h // fy, w // fx; small = refc[:ch * fy, :cw * fx].reshape(ch, fy, cw, fx).mean(axis=(1, 3))
            self.assertLess(np.abs(plane[:ch, :cw] - small).mean(), 1.5, kw)                                                          # chroma on its own grid
        return j, data
    def test_baseline_444(self): self.check(61, 43, quality=90, subsampling=0)
    def test_baseline_420(self): self.check(100, 77, patch=False, quality=85, subsampling=2)
    def test_progressive_444(self): self.check(70, 50, quality=92, subsampling=0, progressive=True)
    def test_progressive_420(self): self.check(130, 95, patch=False, quality=80, subsampling=2, progressive=True)
    def test_progressive_optimized(self): self.check(97, 61, patch=False, quality=75, subsampling=2, progressive=True, optimize=True)
    def test_one_pixel_and_tiny(self):
        for w, h in ((1, 1), (7, 9), (8, 8), (17, 5)): self.check(w, h, quality=90, subsampling=0)
    def test_grayscale(self):
        g = picture(50, 40).convert("L"); data = encode(g, quality=88); j = jpegcoef.read_jpeg(data); self.assertEqual(len(j.components), 1)
        y = jpegcoef.idct_plane(j.components[0], j.quant[j.components[0].tq])[:40, :50]; self.assertLess(np.abs(y - np.asarray(Image.open(io.BytesIO(data)), dtype=np.float64)).mean(), 0.8)
    def test_dc_matches_the_eighth_scale_decode(self):
        j, data = self.check(128, 96, quality=90, subsampling=0, progressive=True)
        im = Image.open(io.BytesIO(data)); im.draft("RGB", (16, 12)); ref = np.asarray(im.convert("RGB")); ry, _, _ = rgb_to_ycc(ref)
        c = j.components[0]; dcy = c.coef[:12, :16, 0] * j.quant[c.tq][0] / 8.0 + 128.0
        self.assertLess(np.abs(dcy - ry[:12, :16]).mean(), 1.0)
    def test_restart_intervals(self):
        try: j, _ = self.check(120, 90, patch=False, quality=85, subsampling=2, restart_marker_blocks=3)
        except TypeError: self.skipTest("this Pillow cannot write restart intervals")
        self.assertGreater(j.restart_interval, 0)
    def test_coefficient_stats(self):
        img = picture(96, 64); j = jpegcoef.read_jpeg(encode(img, quality=90, subsampling=0, progressive=True)); c = j.components[0]
        self.assertGreater(int((c.coef[..., 1:] != 0).sum()), 100); self.assertTrue(np.abs(c.coef).max() < 2048)
        jb = jpegcoef.read_jpeg(encode(img, quality=90, subsampling=0)); self.assertTrue(np.array_equal(jb.components[0].coef, c.coef))     # progressive and baseline encodings of the same picture hold the same coefficients
    def same_coefficients(self, a, b):
        ja, jb = jpegcoef.read_jpeg(a), jpegcoef.read_jpeg(b)
        for ca, cb in zip(ja.components, jb.components): self.assertTrue(np.array_equal(ca.coef, cb.coef))
    def test_420_progressive_and_baseline_hold_the_same_coefficients(self):                           # exact, on the noisy picture: interleaving, true block counts, every refinement pass
        img = picture(131, 97, seed=3)
        self.same_coefficients(encode(img, quality=85, subsampling=2), encode(img, quality=85, subsampling=2, progressive=True))
        self.same_coefficients(encode(img, quality=70, subsampling=2), encode(img, quality=70, subsampling=2, progressive=True, optimize=True))
    def test_restart_intervals_hold_the_same_coefficients(self):
        img = picture(120, 90, seed=4)
        try: r = encode(img, quality=85, subsampling=2, restart_marker_blocks=3)
        except TypeError: self.skipTest("this Pillow cannot write restart intervals")
        self.same_coefficients(encode(img, quality=85, subsampling=2), r)
    def test_errors(self):
        good = encode(picture(40, 30), quality=90)
        with self.assertRaises(ValueError): jpegcoef.read_jpeg(b"not a jpeg")
        with self.assertRaises(ValueError): jpegcoef.read_jpeg(good[:2] + b"\xff\xd9")                                                      # no frame
        bad = good.replace(b"\xff\xc0", b"\xff\xc9", 1)
        with self.assertRaises(ValueError): jpegcoef.read_jpeg(bad)                                                                         # arithmetic coding: refused

def mutants():
    src = (ROOT / "tools/gallery/jpegcoef.py").read_text()
    muts = [("dc pred", "dcpred[id(comp)] += diff; blk[0] = dcpred[id(comp)]\n                    k = 1", "dcpred[id(comp)] = diff; blk[0] = dcpred[id(comp)]\n                    k = 1"),
            ("zrl", "if r == 15: k += 16; continue", "if r == 15: k += 15; continue"),
            ("extend", "return v if v >= (1 << (s - 1)) else v - (1 << s) + 1", "return v if v > (1 << (s - 1)) else v - (1 << s) + 1"),
            ("ac first shift", "blk[k] = br.extend(s) << al; k += 1", "blk[k] = br.extend(s); k += 1"),
            ("eobrun first", "eobrun = (1 << r) + (br.get(r) if r else 0) - 1; break", "eobrun = (1 << r) + (br.get(r) if r else 0); break"),
            ("dc refine", "elif br.bit(): blk[0] |= (1 << al)", "elif br.bit(): blk[0] |= (1 << (al + 1))"),
            ("refine new", "newv = p1 if br.bit() else m1", "newv = p1 if br.bit() else p1"),
            ("refine correction", "if br.bit() and (cv & p1) == 0: blk[k] = cv + (p1 if cv >= 0 else m1)\n                                else:", "if br.bit() and (cv & p1) == 0: blk[k] = cv + p1\n                                else:"),
            ("refine eob", "if cv != 0 and br.bit() and (cv & p1) == 0: blk[k] = cv + (p1 if cv >= 0 else m1)\n                            k += 1\n                        eobrun -= 1", "if cv != 0 and br.bit() and (cv & p1) == 0: blk[k] = cv + (p1 if cv >= 0 else m1)\n                            k += 1\n                        eobrun -= 2"),
            ("stuffing", 'replace(b"\\xff\\x00", b"\\xff")); start = i + 2; i += 2', 'replace(b"\\xff\\x00", b"\\x00")); start = i + 2; i += 2'),
            ("zigzag", "ZZ = np.array([0, 1, 8, 16, 9, 2,", "ZZ = np.array([0, 1, 8, 9, 16, 2,"),
            ("true blocks", "c.true_w = _cdiv(_cdiv(j.width * c.h, hmax), 8)", "c.true_w = _cdiv(_cdiv(j.width * c.h, hmax), 8) + 1"),
            ("idct", "px = np.einsum(\"xu,hwuv,vy->hwxy\", _C.T, f, _C)", "px = np.einsum(\"xu,hwuv,vy->hwxy\", _C.T, f.transpose(0, 1, 3, 2), _C)"),
            ("lut", "for f in range(1 << (9 - l)): self.lut[base + f] = (l, symbols[k])", "for f in range(1 << (9 - l)): self.lut[base + f] = (l, symbols[k - 1])")]
    bad = 0
    for name, a, b in muts:
        assert a in src, name
        mod = types.ModuleType("jpegcoef"); exec(compile(src.replace(a, b, 1), "jpegcoef_mut", "exec"), mod.__dict__); sys.modules["jpegcoef"] = mod; globals()["jpegcoef"] = mod
        r = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.defaultTestLoader.loadTestsFromTestCase(T))
        print(("killed  " if not r.wasSuccessful() else "SURVIVED"), name); bad += r.wasSuccessful()
    return bad

if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    unittest.main()
