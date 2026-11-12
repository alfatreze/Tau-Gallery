#!/usr/bin/env python3
"""Host test of fw2/gallery/gallery_pages.h (sim/test_gallery_pages.c). The unmodified build must pass; the six built-in mutants (-DBUG=1..9: clamping instead of wrapping in the menu, Controls and Help, the menu
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
        for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_pages.h").read_text()
        for a, b in (("if (sel >= top + GC_VIS) top = sel + 1u - GC_VIS;", ""), ("if (sel < top) top = sel;", ""), ("else if (g->sel == GT_MODE) l->pol = hl_wrap(l->pol, 2u, dec);", "else if (g->sel == GT_MODE) l->pol = 0u;"), ("l->pal = hl_wrap(l->pal, TC_PALETTE_N, dec);", "l->pal = hl_wrap(l->pal, TC_PALETTE_N - 1u, dec);"), ("g->sel = (uint8_t)hl_wrap(g->sel, GT_ROWS, (edge & GP_KEY_UP) != 0); return GT_STAY;", "g->sel = (uint8_t)hl_wrap(g->sel, GT_ROWS - 1u, (edge & GP_KEY_UP) != 0); return GT_STAY;"), ("*resume = (*resume & 1u) ^ 1u;", "*resume = 1u;"), ("gi_cap(d->set_loads, 9999u)", "gi_cap(d->set_loads, 99999999u)"), ("m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_THEME;\n#endif", "m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_SETTINGS;\n#endif"), ("return (edge & (GP_KEY_B | GP_KEY_START)) ? GP_CLOSE : GP_STAY;", "return (edge & GP_KEY_B) ? GP_CLOSE : GP_STAY;"),
                     ("return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0;\n}\n\n/* ---- Help", "return (edge & GP_KEY_LEFT) != 0;\n}\n\n/* ---- Help"),
                     ("(edge & (GP_KEY_B | GP_KEY_SELECT)) != 0; }\n#endif", "(edge & (GP_KEY_B | GP_KEY_START)) != 0; }\n#endif"),
                     ("q = gi_us(q, d->zt_land_sum / d->zt_n); q = gi_s(q, \" MAX \");", "q = gi_us(q, d->zt_land_sum); q = gi_s(q, \" MAX \");"),
                     ("q = gi_s(r[12].value, \"AVG \"); q = gi_us(q, d->zt_dec_sum / d->zt_dec_n);", "q = gi_s(r[12].value, \"AVG \"); q = gi_us(q, d->zt_dec_sum / d->zt_n);"),
                     ("if (d->zt_dec_n == 0u) { q = gi_s(r[12].value", "if (d->zt_n == 0u) { q = gi_s(r[12].value"),
                     ("if (us > 999900u) us = 999900u;", ""),
                     ("gi_cap(d->zt_catch > d->zt_moved ? d->zt_catch - d->zt_moved : 0u, 99999u)", "gi_cap(d->zt_catch - d->zt_moved, 99999u)"),
                     ("gi_cap(d->zt_n, 9999u)", "gi_cap(d->zt_n, 99999u)"),
                     ("for (n = 0; d->name[n] && n < 26u; n++)", "for (n = 0; d->name[n] && n < 40u; n++)"),
                     ("{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE\"},", "{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE / MORE TEXT HERE\"},")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:56]); bad += rc == 0
        sys.exit(1 if bad else 0)
