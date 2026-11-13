#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_mask.h (the crop geometry of the picture view's masks): the crop of each mask in a picture rectangle against a floating-point reference (largest rectangle of the ratio that fits, centred, x edges
even), both orientations, the square cannot be turned, the cycle. `--mutants` edits a copy of the header; each edit must fail. Run: python3 sim/test_gallery_mask.py [--mutants]"""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "fw2/gallery/gallery_mask.h"; HARNESS = ROOT / "sim/gallery_mask_harness.c"
RATIO = {1: (1, 1), 2: (5, 4), 3: (3, 2), 4: (16, 9), 5: (239, 100), 6: (65, 24), 7: (3, 1)}
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1
def build(text=None):
    d = Path(tempfile.mkdtemp()); (d / "gallery_mask.h").write_text(text if text is not None else SRC.read_text())
    (d / "h.c").write_text(HARNESS.read_text().replace('#include "../fw2/gallery/gallery_mask.h"', f'#include "{d / "gallery_mask.h"}"'))
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    subprocess.run(["cc", "-O1", "-w", "-o", str(d / "h"), str(d / "h.c")], check=True, env=env); return d / "h"
def run(exe, x, y, w, h): return subprocess.run([str(exe), str(x), str(y), str(w), str(h)], capture_output=True, text=True).stdout.splitlines()
def ref(i, p, x, y, w, h):
    n, d = RATIO[i]
    if p and i > 1: n, d = d, n
    if w * d >= h * n: ch = h; cw = h * n // d
    else: cw = w; ch = w * d // n
    cx = (x + (w - cw) // 2) & ~1; ex = (cx + cw + 1) & ~1
    return (cx, y + (h - ch) // 2, ex, y + (h - ch) // 2 + ch)
def evaluate(exe):
    global fails
    fails = 0
    for (x, y, w, h) in ((0, 93, 800, 533), (280, 0, 240, 720), (0, 0, 800, 720), (40, 0, 720, 800), (50, 100, 301, 199), (0, 226, 800, 267), (2, 2, 10, 10)):
        out = run(exe, x, y, w, h); rows = [tuple(int(v) for v in l.split()) for l in out if l[0].isdigit()]
        ok = len(rows) == 14; bad = []
        for (i, p, x0, y0, x1, y1, ct, nx) in rows:
            e = ref(i, p, x, y, w, h)
            if (x0, y0, x1, y1) != e: ok = False; bad.append((i, p, (x0, y0, x1, y1), e))
        check(f"rect {x},{y} {w}x{h}: every mask in both orientations equals the reference crop" + (f" ({bad[:2]})" if bad else ""), ok)
        inside = all(x <= x0 and x1 <= x + w + 1 and y <= y0 and y1 <= y + h and x0 % 2 == 0 and x1 % 2 == 0 for (_, _, x0, y0, x1, y1, _, _) in rows)
        check(f"rect {x},{y} {w}x{h}: crops stay inside the rectangle (the right edge at most one pixel for evenness), x edges even", inside)
    out = run(exe, 0, 93, 800, 533); rows = [tuple(int(v) for v in l.split()) for l in out if l[0].isdigit()]
    check("the square cannot be turned, every other mask can", all(ct == (0 if i == 1 else 1) for (i, p, _, _, _, _, ct, _) in rows) and [r[2:6] for r in rows if r[0] == 1 and r[1] == 0] == [r[2:6] for r in rows if r[0] == 1 and r[1] == 1])
    check("turning a mask exchanges its ratio: 16:9 landscape is wider than tall, portrait taller than wide", (lambda a, b: a[2] - a[0] > a[3] - a[1] and b[3] - b[1] > b[2] - b[0])([r for r in rows if r[:2] == (4, 0)][0][2:6], [r for r in rows if r[:2] == (4, 1)][0][2:6]))
    check("the cycle: off, then the masks, then off again", out[-1].startswith("next0 1 nextlast 0") and all(r[7] == (r[0] + 1) % 8 for r in rows))
    return fails
def mutants():
    src = SRC.read_text(); muts = [
        ("the turn is ignored", "if (portrait && gm_can_turn(i)) {", "if (0) {"), ("the square is turned too", "static inline int gm_can_turn(uint32_t i) { return i > 1u && i < GM_N; }", "static inline int gm_can_turn(uint32_t i) { return i > 0u && i < GM_N; }"),
        ("the width test uses the wrong side", "if ((uint64_t)w * d >= (uint64_t)h * n) {", "if ((uint64_t)w * n >= (uint64_t)h * d) {"), ("the crop is not centred in x", "uint32_t cx = x + (w - cw) / 2u, cy = y + (h - ch) / 2u;", "uint32_t cx = x, cy = y + (h - ch) / 2u;"),
        ("the crop is not centred in y", "uint32_t cx = x + (w - cw) / 2u, cy = y + (h - ch) / 2u;", "uint32_t cx = x + (w - cw) / 2u, cy = y;"), ("the left edge is not made even", "    cx &= ~1u;                                                            /* even */\n", ""),
        ("the right edge is not made even", "uint32_t ex = (cx + cw + 1u) & ~1u;", "uint32_t ex = cx + cw;"), ("the 4:5 ratio is wrong", "{ 5, 4, \"4:5\" }", "{ 4, 5, \"4:5\" }"),
        ("the cinemascope ratio is 2.35", "{ 239, 100, \"2.39:1 CINEMASCOPE\" }", "{ 235, 100, \"2.39:1 CINEMASCOPE\" }"), ("the xpan ratio is wrong", "{ 65, 24, \"65:24 XPAN\" }", "{ 65, 25, \"65:24 XPAN\" }"),
        ("the cycle does not return to off", "return (i + 1u) % GM_N;", "return i + 1u < GM_N ? i + 1u : 1u;"), ("the crop can exceed the width", "    if (cw > w) cw = w;\n", ""), ("the crop can exceed the height", "    if (ch > h) ch = h;\n", ""),
        ("the y end ignores the height", "*y0 = cy; *y1 = cy + ch;", "*y0 = cy; *y1 = cy + h;")]
    surv = 0
    for name, a, b in muts:
        assert a in src, name
        r = evaluate(build(src.replace(a, b, 1))); print("SURVIVED" if r == 0 else "killed  ", name); surv += r == 0
    return surv
if __name__ == "__main__":
    if "--mutants" in sys.argv: sys.exit(1 if mutants() else 0)
    f = evaluate(build()); print("gallery-mask: %d failure(s)" % f); sys.exit(1 if f else 0)
