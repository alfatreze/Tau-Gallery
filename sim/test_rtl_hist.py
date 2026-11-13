#!/usr/bin/env python3
"""Tests src/fpga/core/tau_hist.sv (the decoder-tap histogram unit) with Icarus against an independent numpy model, and runs its mutants (BUG 1..8: each must fail).
The model counts from the specification in the module header: luma = (77 R + 150 G + 29 B) >> 8, a pixel counts when enabled and its canvas position (origin + position in the tile) is inside the half-open
window, the clear zeroes everything. The stimulus has runs of the same bin (the read-modify-write hazard at distances 1, 2 and 3), alternating bins, valid gaps, saturated values, window edges, en off,
a clear in the middle and counts above 65,535 in one bin. Run: python3 sim/test_rtl_hist.py [--mutants]"""
import subprocess, sys
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "rtl"; OUT.mkdir(parents=True, exist_ok=True)

def luma(r, g, b): return (77 * r + 150 * g + 29 * b) >> 8

def make_vectors(seed=3):
    rng = np.random.default_rng(seed); cmd = []; dumps = []
    cfg = dict(en=0, ox=0, oy=0, x0=0, y0=0, x1=0, y1=0)
    state = np.zeros((4, 256), dtype=np.int64); count = 0
    def line(v, x, y, r, g, b, ev=0, p=(0, 0, 0, 0, 0, 0, 0)): cmd.append((v, x, y, r, g, b, ev) + tuple(p))
    def pix(x, y, r, g, b):
        nonlocal count
        line(1, x, y, r, g, b)
        cx, cy = cfg["ox"] + x, cfg["oy"] + y
        if cfg["en"] and cfg["x0"] <= cx < cfg["x1"] and cfg["y0"] <= cy < cfg["y1"]:
            state[0][luma(r, g, b)] += 1; state[1][r] += 1; state[2][g] += 1; state[3][b] += 1; count += 1
    def idle(n=1):
        for _ in range(n): line(0, 0, 0, 0, 0, 0)
    def setcfg(**kw):
        cfg.update(kw); idle(1); line(0, 0, 0, 0, 0, 0, 1, (cfg["en"], cfg["ox"], cfg["oy"], cfg["x0"], cfg["y0"], cfg["x1"], cfg["y1"])); idle(8)
    def clear():
        nonlocal count
        state[:] = 0; count = 0; line(0, 0, 0, 0, 0, 0, 2); idle(4)
    def dump():
        line(0, 0, 0, 0, 0, 0, 3); dumps.append((state.copy(), count)); idle(4)
    def tile(ox, oy, kind):
        setcfg(ox=ox, oy=oy)
        for y in range(128):
            for x in range(128):
                if kind == 0: r, g, b = rng.integers(0, 256, 3)
                elif kind == 1: r = g = b = 77                                               # one bin, back to back
                elif kind == 2: r, g, b = ((x & 1) * 200 + 5, (x & 1) * 90, 255 - (x & 1) * 255)       # two bins alternating
                elif kind == 3: r, g, b = ((x // 3) % 2 * 255, 128, (x // 2) % 2 * 255)                # runs of 3 and 2
                else: r, g, b = (x * 2) & 255, (y * 2) & 255, ((x + y) * 3) & 255
                pix(x, y, int(r), int(g), int(b))
                if kind == 0 and rng.random() < 0.15: idle(int(rng.integers(1, 4)))               # valid gaps
    idle(10); clear(); dump()                                                                     # a fresh unit: all zero
    setcfg(en=1, x0=0, y0=0, x1=800, y1=720)
    for k in range(5): tile(128 * (k % 3), 128 * (k // 3), k)
    dump()
    setcfg(x0=100, y0=40, x1=500, y1=300)                                                       # a window that cuts through tiles (the black bars of a letterbox)
    for k in range(4): tile(64 * k, 32 * k, 4 if k % 2 else 0)
    dump()
    setcfg(en=0); tile(0, 0, 0); dump()                                                           # en off: nothing counted
    setcfg(en=1, x0=0, y0=0, x1=128, y1=128); clear(); tile(0, 0, 4); dump()                       # cleared, exactly one tile
    setcfg(x0=0, y0=0, x1=129, y1=129); tile(1, 1, 3); dump()                                    # window edges, 1 pixel past
    setcfg(en=1, ox=0, oy=0, x0=10, y0=10, x1=100, y1=100); clear(); tile(0, 0, 4); dump()           # the right and bottom edges fall inside the tile: x1 and y1 themselves are NOT counted
    setcfg(ox=0, oy=0, x0=0, y0=0, x1=800, y1=720); clear()
    for _ in range(2):                                                                            # a bin above 16 bits: 2 x 40,000 pixels of one colour
        setcfg(ox=0, oy=0); 
        for _ in range(40000): pix(5, 5, 9, 9, 9)
    dump()
    (OUT / "hist_cmd.txt").write_text("\n".join(" ".join(str(int(v)) for v in c) for c in cmd) + "\n")
    return len(cmd), dumps

def parse_dumps(text):
    res = []; cur = None
    for ln in text.splitlines():
        if ln.startswith("DUMP"): cur = []
        elif ln.startswith("N "): res.append((np.array(cur[:1024], dtype=np.int64).reshape(4, 256), int(ln.split()[1]))); cur = None
        elif cur is not None and ln.strip(): cur.append(int(ln))
    return res

def run(bug):
    vvp = OUT / f"tb_tau_hist_{bug}.vvp"
    r = subprocess.run(["iverilog", "-g2012", "-Isrc/fpga/core", f"-Ptb_tau_hist.BUG={bug}", "-o", str(vvp), "sim/tb_tau_hist.v", "src/fpga/core/tau_hist.sv"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode: print(r.stderr[:2000]); return None
    r = subprocess.run(["vvp", str(vvp)], cwd=ROOT, capture_output=True, text=True, timeout=900)
    return r.stdout, (OUT / "hist_out.txt").read_text()

def verdict(out_text, expected):
    got = parse_dumps(out_text)
    if len(got) != len(expected): return False, f"{len(got)} dumps, expected {len(expected)}"
    bad = [i for i, ((gb, gn), (eb, en)) in enumerate(zip(got, expected)) if not (np.array_equal(gb, eb) and gn == en)]
    return not bad, ("dumps differ: %s" % bad if bad else "")

if __name__ == "__main__":
    n, expected = make_vectors(); print(f"{n} cycles, {len(expected)} dumps of vectors")
    o = run(0)
    if o is None: sys.exit(1)
    ok, why = verdict(o[1], expected); print(("PASS  BUG=0: all %d dumps equal the model (%d bins and the count each)" % (len(expected), 1024)) if ok else "FAILED  BUG=0: " + why)
    if "--mutants" in sys.argv and ok:
        bad = 0
        for b in range(1, 9):
            o = run(b); killed = o is None or "TIMEOUT" in o[0] or not verdict(o[1], expected)[0]
            print("killed  " if killed else "SURVIVED", f"BUG={b}"); bad += not killed
        sys.exit(1 if bad else 0)
    sys.exit(0 if ok else 1)
