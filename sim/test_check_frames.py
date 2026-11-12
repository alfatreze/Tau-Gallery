#!/usr/bin/env python3
"""Host test for tools/check_frames.py: identical frames pass; one changed pixel, a size change and a missing file fail with the right report; update then compare passes."""
import gzip, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); import check_frames as C
TOOL = [sys.executable, str(ROOT / "tools/check_frames.py")]
fails = 0
def check(name, cond, extra=""):
    global fails
    if not cond: fails += 1; print("FAIL:", name, extra)
def ppm(w, h, px): return b"P6\n%d %d\n255\n" % (w, h) + px
with tempfile.TemporaryDirectory() as t:
    t = Path(t); base = bytearray(8 * 4 * 3); base[3 * 5:3 * 5 + 3] = b"\xf0\xf4\xf0"
    (t / "a.ppm").write_bytes(ppm(8, 4, bytes(base))); (t / "g.ppm.gz").write_bytes(gzip.compress(ppm(8, 4, bytes(base))))
    r = subprocess.run(TOOL + ["compare", str(t / "a.ppm"), str(t / "g.ppm.gz")], capture_output=True, text=True); check("identical passes", r.returncode == 0 and "FRAME OK" in r.stdout, r.stdout)
    one = bytearray(base); one[3 * 20:3 * 20 + 3] = b"\x01\x02\x03"; (t / "b.ppm").write_bytes(ppm(8, 4, bytes(one)))
    r = subprocess.run(TOOL + ["compare", str(t / "b.ppm"), str(t / "g.ppm.gz"), "--diff", str(t / "d.png")], capture_output=True, text=True)
    check("one pixel fails", r.returncode == 1 and "1 of 32 pixels differ" in r.stdout and "x 4..4 y 2..2" in r.stdout, r.stdout); check("diff png written", (t / "d.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n")
    (t / "c.ppm").write_bytes(ppm(4, 8, bytes(base)))
    r = subprocess.run(TOOL + ["compare", str(t / "c.ppm"), str(t / "g.ppm.gz")], capture_output=True, text=True); check("size change fails", r.returncode == 1 and "size 4x8" in r.stdout, r.stdout)
    r = subprocess.run(TOOL + ["compare", str(t / "nope.ppm"), str(t / "g.ppm.gz")], capture_output=True, text=True); check("missing file is exit 2", r.returncode == 2, r.stdout)
    (t / "bad.ppm").write_bytes(b"P6\n8 4\n255\nshort"); r = subprocess.run(TOOL + ["compare", str(t / "bad.ppm"), str(t / "g.ppm.gz")], capture_output=True, text=True); check("truncated file is exit 2", r.returncode == 2, r.stdout)
    r = subprocess.run(TOOL + ["update", str(t / "b.ppm"), str(t / "g2.ppm.gz")], capture_output=True, text=True); check("update", r.returncode == 0)
    r = subprocess.run(TOOL + ["compare", str(t / "b.ppm"), str(t / "g2.ppm.gz")], capture_output=True, text=True); check("compare after update passes", r.returncode == 0, r.stdout)
    check("update is reproducible", (t / "g2.ppm.gz").read_bytes() == gzip.compress(ppm(8, 4, bytes(one)), mtime=0))
print("CHECK_FRAMES TEST", "PASSED" if not fails else "FAILED %d" % fails); sys.exit(1 if fails else 0)
