#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_pages.h (sim/test_gallery_pages.c). The unmodified build must pass; the six built-in mutants (-DBUG=1..6: clamping instead of wrapping in the menu, Controls and Help, the menu
entries swapped) must each fail. The source mutants then drop the window fix and the close keys from copies of the header."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def build(extra, hdr=None):
    w = Path(tempfile.mkdtemp()); (w / "fw2/gallery").mkdir(parents=True); (w / "fw2/cores").mkdir(); (w / "sim").mkdir()
    for f in (ROOT / "fw2/cores").glob("*.h"): (w / "fw2/cores" / f.name).write_text(f.read_text())
    gen = w / "fw2/generated"; gen.mkdir()
    for f in (ROOT / "fw2/generated").glob("*.h"): (gen / f.name).write_text(f.read_text())
    (w / "fw2/gallery/gallery_pages.h").write_text(hdr if hdr is not None else (ROOT / "fw2/gallery/gallery_pages.h").read_text())
    (w / "sim/t.c").write_text((ROOT / "sim/test_gallery_pages.c").read_text())
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", *extra, "-o", str(w / "t"), str(w / "sim/t.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return None
    return w
def run(w):
    r = subprocess.run([str(w / "t")], capture_output=True, text=True); return r.returncode, r.stdout
if __name__ == "__main__":
    w = build([]); 
    if not w: sys.exit(1)
    rc, out = run(w); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l))
    if rc: sys.exit(1)
    if "--mutants" in sys.argv:
        bad = 0
        for n in (1, 2, 3, 4, 5, 6):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_pages.h").read_text()
        for a, b in (("if (sel >= top + GC_VIS) top = sel + 1u - GC_VIS;", ""), ("if (sel < top) top = sel;", ""), ("return (edge & (GP_KEY_B | GP_KEY_START)) ? GP_CLOSE : GP_STAY;", "return (edge & GP_KEY_B) ? GP_CLOSE : GP_STAY;"),
                     ("return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0;\n}\n\n/* ---- Help", "return (edge & GP_KEY_LEFT) != 0;\n}\n\n/* ---- Help"),
                     ("{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE\"},", "{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE / MORE TEXT HERE\"},")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:56]); bad += rc == 0
        sys.exit(1 if bad else 0)
