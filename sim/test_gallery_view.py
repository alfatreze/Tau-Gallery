#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_view.h (sim/test_gallery_view.c): the viewport rules of M2 (ladder, clamp and 4-pixel alignment, zoom mapping, pan speed, the resident window, the tiles a viewport needs, the tile
queue, the 48 KB command limit). The unmodified build must pass; the twenty-nine built-in mutants (-DBUG=1..30) and the source mutants must each fail."""
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
        for n in range(1, 31):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_view.h").read_text()
        for a, b in (("if (v < 0) v = 0;\n    if (align) v = gv_floor4(v);", "if (v < 0) v = 0;"),
                     ("if (align) hi = (hi + 3) & ~3;", "if (align) hi = hi & ~3;"),
                     ("return -(o & ~1);", "return -o;"),
                     ("return held_frames < 8u ? 4 : 8;", "return held_frames < 8u ? 4 : 16;"),
                     ("return held_frames < 8u ? 4 : 8;", "return held_frames < 8u ? 8 : 8;"),
                     ("if ((aheadx && overy) || (aheady && overx)) k = 0x10000000 + ((aheadx && overy) ? gapx : gapy);", "if ((aheadx && overy) || (aheady && overx)) k = 0x10000000 + ((aheadx && overy) ? gapy : gapx);"),
                     ("else if (aheadx || aheady) k = 0x20000000 + gapx + gapy;", "else if (aheadx || aheady) k = 0x20000000 - gapx - gapy;"),
                     ("if (wx > 0 && vx - wx < GV_MARGIN) return 0;", ""),
                     ("if (wy + GV_WIN_H < sh && (wy + GV_WIN_H) - (vy + GV_SCREEN_H) < GV_MARGIN) return 0;", ""),
                     ("static inline uint32_t gv_toggle(const gv_ladder_t *l, uint32_t cur) { return cur == 0u ? l->n - 1u : 0u; }", "static inline uint32_t gv_toggle(const gv_ladder_t *l, uint32_t cur) { return cur == 0u ? 1u : 0u; }"),
                     ("for (uint32_t w = 1u; w < n; w *= 2u) {", "for (uint32_t w = 1u; w < 1u; w *= 2u) {"),
                     ("while (a < mid && b < hi) dst[o++] = key[src[b]] < key[src[a]] ? src[b++] : src[a++];", "while (a < mid && b < hi) dst[o++] = key[src[b]] > key[src[a]] ? src[b++] : src[a++];"),
                     ("    for (uint32_t i = 0; i < n; i++) out[i] = tmp[i];\n    return n;", "    return n;"),
                     ("while (b < hi) dst[o++] = src[b++];", ""),
                     ("*vx = (int32_t)((int64_t)(rx - fox) * sk / sf + okx);", "*vx = (int32_t)((int64_t)(rx - fox) * sk / sf);"),
                     ("*vy = (int32_t)((int64_t)(ry - foy) * sk / sf + oky);", "*vy = (int32_t)((int64_t)(ry - fox) * sk / sf + oky);"),
                     ("if (rw >= pw) *rx = gv_floor4(px - (rw - pw) / 2);", "if (rw >= pw) *rx = px;"),
                     ("if (rh >= ph) *ry = py - (rh - ph) / 2;", "if (rh >= ph) *ry = py;"),
                     ("const int32_t lo = (px + 3) & ~3, hi = gv_floor4(px + pw - rw);", "const int32_t lo = px, hi = px + pw - rw;"),
                     ("const int32_t lo = py, hi = py + ph - rh;", "const int32_t lo = py, hi = py + ph - rh + 8;"),
                     ("w &= ~(int64_t)1; h &= ~(int64_t)1;", "h &= ~(int64_t)1;"),
                     ("out[n].ring = on ? 0u : 1u;", "out[n].ring = 0u;"),
                     ("if (q->n < GV_MAX_TILES && !gv_q_has(q, t.tx, t.ty))", "if (q->n < GV_MAX_TILES)")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:60]); bad += rc == 0
        sys.exit(1 if bad else 0)
