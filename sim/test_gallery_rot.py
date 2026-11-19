#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_rot.h (the orientation maths of the hardware decoder's scatter writer) against numpy's own rotations: the displayed picture built pixel by pixel through the firmware's map equals
numpy's transpose / flip / rot90 of the picture; the scatter placement (a pixel at tile offset + x * stx + y * sty) lands every pixel of every tile of the picture on its place; the turn and mirror tables agree with
the matrices. `--mutants` edits a copy of the header; each edit must fail this test. Run: python3 sim/test_gallery_rot.py [--mutants]"""
import os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "fw2/gallery/gallery_rot.h"; HARNESS = ROOT / "sim/gallery_rot_harness.c"
NP = {0: lambda a: a, 1: lambda a: a[:, ::-1], 2: lambda a: a[::-1, :], 3: lambda a: a[::-1, ::-1], 4: lambda a: a.transpose(1, 0, 2), 5: lambda a: np.rot90(a, 1), 6: lambda a: np.rot90(a, -1), 7: lambda a: a.transpose(1, 0, 2)[::-1, ::-1]}
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1
def build(text=None):
    d = Path(tempfile.mkdtemp()); (d / "gallery_rot.h").write_text(text if text is not None else SRC.read_text())
    h = HARNESS.read_text().replace('#include "../fw2/gallery/gallery_rot.h"', f'#include "{d / "gallery_rot.h"}"'); (d / "h.c").write_text(h)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    subprocess.run(["cc", "-O1", "-w", "-o", str(d / "h"), str(d / "h.c"), "-lm"], check=True, env=env); return d / "h"
def run(exe, iw, ih, pitch): return subprocess.run([str(exe), str(iw), str(ih), str(pitch)], capture_output=True, text=True).stdout.splitlines()
def evaluate(exe):
    global fails
    fails = 0
    for iw, ih in ((7, 5), (16, 9), (3, 11), (1, 6)):
        pitch = 3 * 40 + 5 * (iw % 3); img = np.arange(iw * ih * 3, dtype=np.uint32).reshape(ih, iw, 3) % 251; img = img.astype(np.uint8)
        lines = run(exe, iw, ih, pitch); heads = [l.split() for l in lines if l.startswith("o ")]; maps = {}; tiles = []
        for l in lines:
            p = l.split()
            if p[0] == "m": maps[(int(p[1]), int(p[2]), int(p[3]))] = (int(p[4]), int(p[5]))
            elif p[0] == "t": tiles.append(tuple(int(v) for v in p[1:]))
        for h in heads:
            o = int(h[1]); dw, dh = int(h[3]), int(h[4]); want = NP[o](img)
            check(f"{iw}x{ih} o{o}: displayed size {want.shape[1]}x{want.shape[0]}", (dw, dh) == (want.shape[1], want.shape[0]))
            got = np.zeros_like(want); inside = True
            for y in range(ih):
                for x in range(iw):
                    X, Y = maps[(o, x, y)]
                    if X >= want.shape[1] or Y >= want.shape[0]: inside = False
                    else: got[Y, X] = img[y, x]
            check(f"{iw}x{ih} o{o}: the map builds numpy's picture", inside and np.array_equal(got, want))
            # the scatter placement: tile offset + x * stx + y * sty, in a byte plane of the displayed picture with pitch `pitch`
            stx, sty = int(h[6]), int(h[8]); plane = np.zeros(dh * pitch + 8, dtype=np.uint8)
            for (to, sx, sy, off) in tiles:
                if to != o: continue
                for y in range(sy, min(sy + 7, ih)):
                    for x in range(sx, min(sx + 5, iw)):
                        a = off + (x - sx) * stx + (y - sy) * sty
                        if 0 <= a and a + 3 <= len(plane): plane[a:a + 3] = img[y, x]
            # only the pixels the sampled tiles cover are checked
            ok = True
            for (to, sx, sy, off) in tiles:
                if to != o: continue
                for y in range(sy, min(sy + 7, ih)):
                    for x in range(sx, min(sx + 5, iw)):
                        X, Y = maps[(o, x, y)]
                        if X >= want.shape[1] or Y >= want.shape[0] or not np.array_equal(plane[Y * pitch + 3 * X:Y * pitch + 3 * X + 3], img[y, x]): ok = False
            check(f"{iw}x{ih} o{o}: the scatter steps put every pixel of the tiles on its place", ok)
        # the turn tables
        cw = {int(h[1]): int(h[10]) for h in heads}; ccw = {int(h[1]): int(h[12]) for h in heads}; mir = {int(h[1]): int(h[14]) for h in heads}; flp = {int(h[1]): int(h[16]) for h in heads}
        ok = all(np.array_equal(NP[cw[o]](img), np.rot90(NP[o](img), -1)) for o in range(8)) and all(np.array_equal(NP[ccw[o]](img), np.rot90(NP[o](img), 1)) for o in range(8)) and all(np.array_equal(NP[mir[o]](img), NP[o](img)[:, ::-1]) for o in range(8)) and all(np.array_equal(NP[flp[o]](img), NP[o](img)[::-1, :]) for o in range(8))
        check(f"{iw}x{ih}: turning clockwise, counter-clockwise and mirroring the displayed picture give the orientation numpy gives", ok)
        check(f"{iw}x{ih}: four turns are the identity", all(cw[cw[cw[cw[o]]]] == o for o in range(8)) and all(ccw[cw[o]] == o for o in range(8)))
    return fails
def mutants():
    src = SRC.read_text(); muts = [
        ("a mirror does not move the corner", "if (gr_a[o][0] < 0) cx += w1;", "if (0) cx += w1;"), ("y corner of x", "if (gr_a[o][1] < 0) cx += h1;", "if (gr_a[o][1] < 0) cx += w1;"),
        ("the second row of the matrix", "if (gr_a[o][2] < 0) cy += w1;", "if (gr_a[o][2] < 0) cy += h1;"), ("rotated 90 clockwise as counter", "{ 0, -1, 1, 0 }, { 0, -1, -1, 0 } };", "{ 0, 1, -1, 0 }, { 0, -1, -1, 0 } };"),
        ("the width and height exchange", "*dw = gr_swaps(o) ? ih : iw;", "*dw = iw;"), ("the x step", "return gr_a[o][0] * 3 + gr_a[o][2] * pitch;", "return gr_a[o][0] * 3 + gr_a[o][3] * pitch;"),
        ("the y step", "return gr_a[o][1] * 3 + gr_a[o][3] * pitch;", "return gr_a[o][1] * 3 + gr_a[o][2] * pitch;"), ("the tile offset uses the width for the pitch", "return Y * pitch + X * 3u;", "return Y * 3u + X * pitch;"),
        ("the turn goes the other way", "const int32_t r00 = 0, r01 = cw ? -1 : 1, r10 = cw ? 1 : -1, r11 = 0;", "const int32_t r00 = 0, r01 = cw ? 1 : -1, r10 = cw ? -1 : 1, r11 = 0;"),
        ("the mirror is a flip top-bottom", "return gr_find(-gr_a[o][0], -gr_a[o][1], gr_a[o][2], gr_a[o][3]);", "return gr_find(gr_a[o][0], gr_a[o][1], -gr_a[o][2], -gr_a[o][3]);"), ("the flip is a mirror left-right", "return gr_find(gr_a[o][0], gr_a[o][1], -gr_a[o][2], -gr_a[o][3]); }", "return gr_find(-gr_a[o][0], -gr_a[o][1], gr_a[o][2], gr_a[o][3]); }"), ("the flip is a rotation by 180", "return gr_find(gr_a[o][0], gr_a[o][1], -gr_a[o][2], -gr_a[o][3]); }", "return gr_find(-gr_a[o][0], -gr_a[o][1], -gr_a[o][2], -gr_a[o][3]); }"), ("the flip keeps the orientation", "return gr_find(gr_a[o][0], gr_a[o][1], -gr_a[o][2], -gr_a[o][3]); }", "return o; }"), ("a swap flag for 3", "return o >= 4u;", "return o >= 5u;")]
    surv = 0
    for name, a, b in muts:
        assert a in src, name
        r = evaluate(build(src.replace(a, b))); print("SURVIVED" if r == 0 else "killed  ", name); surv += r == 0
    return surv
if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    f = evaluate(build()); print("gallery-rot: %d failure(s)" % f); sys.exit(1 if f else 0)
