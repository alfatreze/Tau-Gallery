#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_lut.h (the colour presets). The C table of every preset is compared with an independent model written from the comments of the header
(exact fractions, clamped), plus properties (white and black ends, monotone, neutral channel untouched, word layout). Source mutants must each fail. Run: python3 sim/test_gallery_lut.py [--mutants]"""
import os, subprocess, sys, tempfile
from fractions import Fraction
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
PROG = r'''#include <stdio.h>
#include "gallery_lut.h"
int main(void){ for (unsigned p = 0; p < 8; p++) for (unsigned c = 0; c < 3; c++) { for (unsigned i = 0; i < 256; i++) printf("%u ", gl_value(p, c, i)); printf("\n"); }
  for (unsigned p = 0; p < 8; p++) printf("%s\n", gl_names[p]); printf("%u %u %u\n", gl_word(2, 255, 7), gl_word(1, 1, 300), gl_word(5, 0, 0)); printf("%u %u\n", gl_value(9, 0, 100) == gl_value(1, 0, 100), gl_value(15, 2, 50) == gl_value(7, 2, 50)); return 0; }'''
def fl(q): return int(q // 1)
def cl(v): return max(0, min(255, v))
def model(p, c, x):
    if p == 1: return cl(x + fl(Fraction(32 * (255 - x) + 127, 255)))
    if p == 2: return cl(128 + fl(Fraction((x - 128) * 5, 4)) if x >= 128 else 128 - fl(Fraction((128 - x) * 5, 4)))
    if p == 3: return cl(x + fl(Fraction(x * (255 - x), 512)))
    if p == 4: return cl(24 + fl(Fraction(x * 231, 255)))
    if p == 5: return cl(fl(Fraction(x * 17, 16))) if c == 0 else cl(fl(Fraction(x * 7, 8))) if c == 2 else x
    if p == 6: return cl(fl(Fraction(x * 17, 16))) if c == 2 else cl(fl(Fraction(x * 7, 8))) if c == 0 else x
    if p == 7: return 255 - x
    return x
def build_run(hdr):
    w = Path(tempfile.mkdtemp()); (w / "gallery_lut.h").write_text(hdr); (w / "t.c").write_text(PROG)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-w", "-o", str(w / "t"), str(w / "t.c"), "-lm"], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:600]); return None
    return subprocess.run([str(w / "t")], capture_output=True, text=True).stdout
def check(out):
    if out is None: return ["does not build"]
    ln = out.splitlines(); bad = []
    for p in range(8):
        for c in range(3):
            got = [int(v) for v in ln[p * 3 + c].split()]
            exp = [model(p, c, x) for x in range(256)]
            if got != exp: bad.append(f"preset {p} channel {c}: first difference at {[i for i in range(256) if got[i] != exp[i]][:3]}")
            if p not in (0, 7) and any(b < a for a, b in zip(got, got[1:])): bad.append(f"preset {p} channel {c} not monotone")
            if p in (1, 3, 5, 6) and got[255] != 255 and not (p == 5 and c == 2) and not (p == 6 and c == 0): bad.append(f"preset {p} channel {c}: white moved")
            if p == 7 and (got[0] != 255 or got[255] != 0): bad.append("invert ends")
            if p in (5, 6) and c == 1 and got != list(range(256)): bad.append("tint touches green")
    if ln[24:32] != ["NORMAL", "BRIGHT", "CONTRAST", "MIDTONES", "FADED", "WARM", "COOL", "INVERT"]: bad.append("names")
    if ln[32].split() != [str((2 << 16) | (255 << 8) | 7), str((1 << 16) | (1 << 8) | 44), str((1 << 16))]: bad.append(f"word layout {ln[32]}")
    if ln[33].split() != ["1", "1"]: bad.append("a preset above 7 does not wrap")
    return bad
if __name__ == "__main__":
    src = (ROOT / "fw2/gallery/gallery_lut.h").read_text(); bad = check(build_run(src))
    print("PASSED" if not bad else "FAILED " + "; ".join(bad[:6]))
    if bad: sys.exit(1)
    if "--mutants" in sys.argv:
        surv = 0
        for a, b in (("x + (32 * (255 - x) + 127) / 255", "x + (32 * (255 - x)) / 255"), ("((x - 128) * 5) / 4 + 128", "((x - 128) * 3) / 2 + 128"), ("x + (x * (255 - x)) / 512", "x + (x * (255 - x)) / 256"),
                     ("24 + (x * 231) / 255", "23 + (x * 231) / 255"), ("ch == 0u ? gl_clamp((x * 17) / 16) : ch == 2u", "ch == 1u ? gl_clamp((x * 17) / 16) : ch == 2u"), ("case GL_WARM:     return ch == 0u", "case GL_WARM:     return ch == 2u"),
                     ("(x * 7) / 8) : (uint32_t)x;\n    case GL_COOL", "(x * 7) / 8) : (uint32_t)(x + 1);\n    case GL_COOL"), ("return (uint32_t)(255 - x);", "return (uint32_t)(254 - x);"), ("v < 0 ? 0u : v > 255 ? 255u", "v < 0 ? 0u : v > 256 ? 256u"),
                     ("v > 255 ? 255u : (uint32_t)v;", "v > 255 ? 254u : (uint32_t)v;"), ("((ch & 3u) << 16) | ((i & 255u) << 8)", "((ch & 3u) << 17) | ((i & 255u) << 8)"), ("i & 255u", "i & 127u"), ("preset % GL_PRESETS", "preset"),
                     ('"MIDTONES", "FADED"', '"FADED", "MIDTONES"')):
            assert a in src, a; k = bool(check(build_run(src.replace(a, b, 1)))); print("killed  " if k else "SURVIVED", a[:60]); surv += not k
        sys.exit(1 if surv else 0)
