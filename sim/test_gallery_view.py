#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_view.h (sim/test_gallery_view.c): the viewport rules of M2 (ladder, clamp and 4-pixel alignment, zoom mapping, pan speed, the resident window, the tiles a viewport needs, the tile
queue, the 48 KB command limit). The unmodified build must pass; the fourteen built-in mutants (-DBUG=1..14) and the source mutants must each fail."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
def build(extra, hdr=None):
    w = Path(tempfile.mkdtemp()); (w / "fw2/gallery").mkdir(parents=True); (w / "sim").mkdir()
    (w / "fw2/gallery/gallery_view.h").write_text(hdr if hdr is not None else (ROOT / "fw2/gallery/gallery_view.h").read_text())
    (w / "sim/t.c").write_text((ROOT / "sim/test_gallery_view.c").read_text())
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", *extra, "-o", str(w / "t"), str(w / "sim/t.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:2000]); return None
    return w
def run(w):
    r = subprocess.run([str(w / "t")], capture_output=True, text=True); return r.returncode, r.stdout
if __name__ == "__main__":
    w = build([])
    if not w: sys.exit(1)
    rc, out = run(w); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l))
    if rc: sys.exit(1)
    if "--mutants" in sys.argv:
        bad = 0
        for n in range(1, 15):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_view.h").read_text()
        for a, b in (("if (v < 0) v = 0;\n    if (align) v = gv_floor4(v);", "if (v < 0) v = 0;"),
                     ("if (align) hi = (hi + 3) & ~3;", "if (align) hi = hi & ~3;"),
                     ("return -(o & ~1);", "return -o;"),
                     ("return held_frames < 8u ? 4 : held_frames < 24u ? 8 : 16;", "return held_frames < 8u ? 4 : held_frames < 24u ? 16 : 16;"),
                     ("if (wx > 0 && vx - wx < GV_MARGIN) return 0;", ""),
                     ("if (wy + GV_WIN_H < sh && (wy + GV_WIN_H) - (vy + GV_SCREEN_H) < GV_MARGIN) return 0;", ""),
                     ("static inline uint32_t gv_toggle(const gv_ladder_t *l, uint32_t cur) { return cur == 0u ? l->n - 1u : 0u; }", "static inline uint32_t gv_toggle(const gv_ladder_t *l, uint32_t cur) { return cur == 0u ? 1u : 0u; }"),
                     ("while (i > 0u && key[i - 1u] > k)", "while (0)"),
                     ("out[i].ring = on ? 0u : 1u;", "out[i].ring = 0u;"),
                     ("if (q->n < GV_MAX_TILES && !gv_q_has(q, t.tx, t.ty))", "if (q->n < GV_MAX_TILES)")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:60]); bad += rc == 0
        sys.exit(1 if bad else 0)
