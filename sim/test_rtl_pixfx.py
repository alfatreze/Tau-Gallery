#!/usr/bin/env python3
"""Tests src/fpga/core/tau_pixfx.sv (the viewer's colour tables and ordered dither) with Icarus against an independent numpy model, and runs its mutants (BUG 1..10: each must fail).
The model is written from the specification in the module header, not from the RTL: a recursive Bayer matrix (the RTL uses a bit trick), thresholds m >> 3 for red and blue and m >> 4 for green, saturation at 255,
the temporal mode's four matrix positions, tables loaded by stores that take effect in blanking. The script is a stream of clk_vid cycles with the events placed in blanking, spaced apart.
Run: python3 sim/test_rtl_pixfx.py [--mutants]"""
import subprocess, sys, os, tempfile
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "rtl"; OUT.mkdir(parents=True, exist_ok=True)

def bayer_recursive(n):
    """The ordered-dither matrix of side n (a power of two) by the textbook recursion; entries 0 .. n*n-1."""
    if n == 1: return np.zeros((1, 1), dtype=np.int64)
    m = bayer_recursive(n // 2); return np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
B8 = bayer_recursive(8)      # B8[y][x]
assert sorted(B8.flatten().tolist()) == list(range(64))
for _y in range(8):                                  # the bit formula of the RTL, written here as a check of the specification text: m = {x0^y0, y0, x1^y1, y1, x2^y2, y2}
    for _x in range(8):
        _m = sum((((_x >> i) ^ (_y >> i)) & 1) << (5 - 2 * i) | (((_y >> i) & 1) << (4 - 2 * i)) for i in range(3))
        assert _m == B8[_y][_x], (_x, _y, _m, B8[_y][_x])

def model_pixel(r, g, b, x, y, f, mode, lut_on, luts):
    if lut_on: r, g, b = luts[0][r], luts[1][g], luts[2][b]
    if mode == 0: tr = tg = 0
    else:
        xs, ys = x, y
        if mode == 2: xs ^= f & 3; ys ^= ((f & 1) << 2) | ((f >> 1) & 1)          # {f0, 0, f1}: bit 2 = f0, bit 0 = f1
        m = int(B8[ys][xs]); tr = m >> 3; tg = m >> 4
    return min(r + tr, 255), min(g + tg, 255), min(b + tr, 255)

def make_vectors(seed=5):
    rng = np.random.default_rng(seed); luts = [list(range(256)) for _ in range(3)]
    mode, lut_on = 0, 0; cmd = []; exp = []
    def cyc(r, g, b, x, y, f, fl, ev=(0, 0, 0, 0)):
        cmd.append((r, g, b, x, y, f, fl) + ev)
        if fl & 1: exp.append(model_pixel(r, g, b, x, y, f, mode, lut_on, luts) + (fl,))
        else: exp.append((0, 0, 0, fl))
    def blank(n, hs=0, vs=0):
        for _ in range(n): cyc(0, 0, 0, 0, 0, 0, (vs << 2) | (hs << 1))
    def store(ch, idx, val):
        luts[ch][idx] = val; cyc(0, 0, 0, 0, 0, 0, 0, (1, ch, idx, val)); blank(16)
    def setmode(m, l):
        nonlocal mode, lut_on
        mode, lut_on = m, l; cyc(0, 0, 0, 0, 0, 0, 0, (2, m, l, 0)); blank(10)
    def lines(count, w, fphase, kind):
        for ly in range(count):
            for x in range(w):
                if kind == 0: r, g, b = rng.integers(0, 256, 3)
                elif kind == 1: r = g = b = (x * 255) // max(w - 1, 1)                         # a ramp
                elif kind == 2: r, g, b = min(250 + (x & 7), 255), min(253 + (x & 3), 255), min(248 + (ly & 7), 255)           # near the top: saturation
                else: r, g, b = (x * 3 + ly) & 255, (x * 5) & 255, (ly * 7) & 255
                cyc(int(r), int(g), int(b), x & 7, ly & 7, fphase, 1 | ((ly % 5 == 0) << 1) | ((ly % 7 == 0) << 2))
            blank(6, hs=1)
    blank(20)
    for kind in (0, 1, 2, 3):                                                                  # mode off, identity tables
        lines(6, 96, 0, kind)
    setmode(1, 0)
    for kind in (0, 1, 2, 3): lines(8, 96, 0, kind)                                            # ordered dither, tables bypassed
    setmode(2, 0)
    for fp in range(4): lines(5, 96, fp, 1); lines(3, 96, fp, 0)                               # temporal: the four frame phases
    for ch in range(3):                                                                        # a gamma-like table per channel, and one inverted, loaded in blanking
        for i in range(256):
            v = int(255 * (i / 255) ** (0.6 + 0.4 * ch)) if ch < 2 else 255 - i
            store(ch, i, v)
        blank(10)
    setmode(1, 1); lines(8, 96, 0, 0); lines(8, 96, 0, 1); lines(4, 96, 0, 2)                   # tables on, dither on
    setmode(0, 1); lines(6, 96, 0, 0); lines(4, 96, 0, 1)                                      # tables on, dither off
    setmode(2, 1); lines(4, 96, 3, 3)
    for _ in range(30):                                                                        # random single entries
        ch, idx = int(rng.integers(0, 3)), int(rng.integers(0, 256)); store(ch, idx, int(rng.integers(0, 256)))
    lines(6, 96, 2, 0)
    blank(8)
    (OUT / "pixfx_cmd.txt").write_text("\n".join(" ".join(str(v) for v in c) for c in cmd) + "\n")
    (OUT / "pixfx_exp.txt").write_text("\n".join(" ".join(str(v) for v in e) for e in exp) + "\n")
    return len(cmd)

def run(bug):
    vvp = OUT / f"tb_tau_pixfx_{bug}.vvp"
    src = ["sim/tb_tau_pixfx.v", "src/fpga/core/tau_pixfx.sv"]
    r = subprocess.run(["iverilog", "-g2012", "-Isrc/fpga/core", f"-Ptb_tau_pixfx.BUG={bug}", "-o", str(vvp)] + src, cwd=ROOT, capture_output=True, text=True)
    if r.returncode: print(r.stderr[:2000]); return None
    r = subprocess.run(["vvp", str(vvp)], cwd=ROOT, capture_output=True, text=True, timeout=600); return r.stdout

if __name__ == "__main__":
    n = make_vectors(); print(f"{n} cycles of vectors written")
    out = run(0)
    if out is None: sys.exit(1)
    print("\n".join(l for l in out.splitlines() if l.strip().startswith(("FAIL", "PASS", "FAILED", "TIMEOUT", "got")))[:1500])
    ok = "PASS  BUG=0" in out
    if "--mutants" in sys.argv and ok:
        bad = 0
        for b in range(1, 11):
            o = run(b); killed = o is None or "FAILED" in o or "TIMEOUT" in o or "PASS  BUG" not in o
            print("killed  " if killed else "SURVIVED", f"BUG={b}"); bad += not killed
        sys.exit(1 if bad else 0)
    sys.exit(0 if ok else 1)
