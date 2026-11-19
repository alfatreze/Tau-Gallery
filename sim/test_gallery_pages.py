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
    r = subprocess.run(["cc", "-O1", "-w", *extra, "-o", str(w / "t"), str(w / "sim/t.c"), "-lm"], capture_output=True, text=True, env=env)
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
        for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21):
            w = build([f"-DBUG={n}"]); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", f"BUG={n}"); bad += rc == 0
        src = (ROOT / "fw2/gallery/gallery_pages.h").read_text()
        for a, b in (("if (sel >= top + GC_VIS) top = sel + 1u - GC_VIS;", ""), ("if (sel < top) top = sel;", ""), ("else if (g->sel == GT_MODE) l->pol = hl_wrap(l->pol, 2u, dec);", "else if (g->sel == GT_MODE) l->pol = 0u;"), ("l->pal = hl_wrap(l->pal, TC_PALETTE_N, dec);", "l->pal = hl_wrap(l->pal, TC_PALETTE_N - 1u, dec);"), ("g->sel = (uint8_t)hl_wrap(g->sel, GT_ROWS, (edge & GP_KEY_UP) != 0); return GT_STAY;", "g->sel = (uint8_t)hl_wrap(g->sel, GT_ROWS - 1u, (edge & GP_KEY_UP) != 0); return GT_STAY;"), ("*resume = (*resume & 1u) ^ 1u;", "*resume = 1u;"), ("gi_cap(d->set_loads, 9999u)", "gi_cap(d->set_loads, 99999999u)"), ("m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_THEME;\n#endif", "m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_SETTINGS;\n#endif"), ("return (edge & (GP_KEY_B | GP_KEY_START)) ? GP_CLOSE : GP_STAY;", "return (edge & GP_KEY_B) ? GP_CLOSE : GP_STAY;"),
                     ("else if (s->row == GSP_GRID) *grid = hl_wrap(*grid % GSP_GRIDS, GSP_GRIDS, dec);", "else if (s->row == GSP_GRID) *grid = (*grid + 1u) % GSP_GRIDS;"),
                     ("else if (s->row == GSP_DITHER) *dither = hl_wrap(*dither % GSP_DITHERS, GSP_DITHERS, dec);", "else if (s->row == GSP_DITHER) *dither = hl_wrap(*dither % GSP_DITHERS, GSP_DITHERS + 1u, dec);"),
                     ("    while ((mx >> s) > 65535u) s++;\n#endif", "#endif"),
                     ("(v == 0u && c != 0u) ? 1u : v", "v"),
                     ("if (c[k][i] > mx) mx = c[k][i];\n    const uint32_t s", "if (c[k][i] > mx && k != 0u) mx = c[k][i];\n    const uint32_t s"),
                     ("h->fine[k][i] = hs_scale(c[k][i], s);\n    h->n = n; h->is_fine = 1u;", "h->fine[k][i] = hs_scale(c[k][i], s);\n    h->n = 0u; h->is_fine = 1u;"),
                     ("h->fine[k][i] = hs_scale(c[k][i], s);\n    h->n = n; h->is_fine = 1u;", "h->fine[k][i] = hs_scale(c[k][i], s);\n    h->n = n;"),
                     ("return h->is_fine ? h->fine[k][i & 255u] : h->bin[k][(i & 255u) >> 3];", "return h->is_fine ? h->fine[k][i & 255u] : h->bin[k][(i & 255u) >> 2];"),
                     ("return h->is_fine ? h->fine[k][i & 255u] : h->bin[k][(i & 255u) >> 3];", "return h->is_fine ? h->bin[k][(i & 255u) >> 3] : h->fine[k][i & 255u];"),
                     ("return h->is_fine ? hs_fmax(h, k) : hs_max(h, k);", "return hs_max(h, k);"),
                     ("h->n = 0u; h->is_fine = 0u; }", "h->n = 0u; }"),
                     ("else *lut = hl_wrap(*lut % GSP_LUTS, GSP_LUTS, dec);", "else *lut = hl_wrap(*lut % GSP_LUTS, GSP_LUTS - 1u, dec);"),
                     ("else *lut = hl_wrap(*lut % GSP_LUTS, GSP_LUTS, dec);", "else *lut = (*lut + 1u) % GSP_LUTS;"),
                     ("else *lut = hl_wrap(*lut % GSP_LUTS, GSP_LUTS, dec);", "else *lut = hl_wrap(*lut, GSP_LUTS, dec);"),
                     ("s->row = (uint8_t)hl_wrap(s->row, GSP_ROWS, (edge & GP_KEY_UP) != 0); return 0;", "s->row = (uint8_t)hl_wrap(s->row, GSP_ROWS - 1u, (edge & GP_KEY_UP) != 0); return 0;"),
                     ("{ \"4 X 3\", \"5 X 4\", \"6 X 4\", \"8 X 6\" }", "{ \"4 X 3\", \"5 X 4\", \"6 X 4\", \"8 X 5\" }"),
                     ("if (i + 2u + len > n) break;", ""), ("else if (tag == 7u && len == 4u)", "else if (tag == 7u)"), ("m->mon >= 1u && m->mon <= 12u", "m->mon >= 1u"), ("if (m->shutter_n && m->shutter_d) m->has |= GDM_SHUTTER;", "m->has |= GDM_SHUTTER;"),
                     ("if (m->shutter_n == 1u) { q = gi_s(q, \"1/\");", "if (m->shutter_n == 0u) { q = gi_s(q, \"1/\");"), ("else if (m->shutter_d == 1u) q = gi_u(q, m->shutter_n);", "else if (m->shutter_d == 0u) q = gi_u(q, m->shutter_n);"),
                     ("q = gi_u(q, m->aperture10 / 10u); *q++ = '.'; q = gi_u(q, m->aperture10 % 10u);", "q = gi_u(q, m->aperture10 % 10u); *q++ = '.'; q = gi_u(q, m->aperture10 / 10u);"), ("if (m->focal10 % 10u) {", "if (0) {"),
                     ("q = gd_u2(q, m->mon);", "q = gi_u(q, m->mon);"), ("if (m->has & GDM_SHOT) p->id[p->n++] = GDP_SHOT;", "p->id[p->n++] = GDP_SHOT;"),
                     ("hl_wrap(p->cur, p->n, (edge & GP_KEY_LEFT) != 0)", "hl_wrap(p->cur, p->n, (edge & GP_KEY_RIGHT) != 0)"), ("    return (edge & (GP_KEY_B | GP_KEY_SELECT)) != 0;\n}\nstatic inline char *gdp_indicator", "    return (edge & GP_KEY_B) != 0;\n}\nstatic inline char *gdp_indicator"),
                     ("q = gi_u(q, (uint32_t)p->cur + 1u);", "q = gi_u(q, (uint32_t)p->cur);"), ("for (uint32_t i = 0; m->camera[i] && i < 26u; i++)", "for (uint32_t i = 0; m->camera[i] && i < 40u; i++)"),
                     ("if (m->has & GDM_TAKEN) {\n        r[n].label = \"TAKEN\";", "if (0) {\n        r[n].label = \"TAKEN\";"),
                     ("return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0;\n}\n\n/* ---- Help", "return (edge & GP_KEY_LEFT) != 0;\n}\n\n/* ---- Help"),
                     ("(edge & (GP_KEY_B | GP_KEY_SELECT)) != 0; }\n#endif", "(edge & (GP_KEY_B | GP_KEY_START)) != 0; }\n#endif"),
                     ("q = gi_us(q, d->zt_land_sum / d->zt_n); q = gi_s(q, \" MAX \");", "q = gi_us(q, d->zt_land_sum); q = gi_s(q, \" MAX \");"),
                     ("q = gi_s(r[13].value, \"AVG \"); q = gi_us(q, d->zt_dec_sum / d->zt_dec_n);", "q = gi_s(r[13].value, \"AVG \"); q = gi_us(q, d->zt_dec_sum / d->zt_n);"),
                     ("if (d->zt_dec_n == 0u) { q = gi_s(r[13].value", "if (d->zt_n == 0u) { q = gi_s(r[13].value"),
                     ("gi_cap(d->zt_n > d->zt_dec_n ? d->zt_n - d->zt_dec_n : 0u, 9999u)", "gi_cap(d->zt_n - d->zt_dec_n, 9999u)"),
                     ("d->zt_stall_run_max == 1u ? \" FRAME IN A ROW\" : \" FRAMES IN A ROW\"", "\" FRAMES IN A ROW\""),
                     ("d->pipe_off ? \"OFF\" : \"ON\"", "d->pipe_off ? \"ON\" : \"OFF\""),
                     ("(edge & GP_KEY_A) ? 2 : 0; }", "0; }"),
                     ("if (us > 999900u) us = 999900u;", ""),
                     ("gi_cap(d->zt_catch > d->zt_moved ? d->zt_catch - d->zt_moved : 0u, 99999u)", "gi_cap(d->zt_catch - d->zt_moved, 99999u)"),
                     ("gi_cap(d->zt_n, 9999u)", "gi_cap(d->zt_n, 99999u)"),
                     ("uint32_t ms = (us + 500u) / 1000u;", "uint32_t ms = us / 1000u;"),
                     ("ms > 99u ? 99u : ms", "ms"),
                     ("q = gi_s(q, \" J\"); q = gi_ms0(q, d->zt_sj);", "q = gi_s(q, \" J\"); q = gi_ms0(q, d->zt_sn);"),
                     ("if (d->zt_pass_max == 0u) { q = gi_s(r[18].value", "if (d->zt_n == 0u) { q = gi_s(r[18].value"),
                     ("for (n = 0; d->name[n] && n < 26u; n++)", "for (n = 0; d->name[n] && n < 40u; n++)"),
                     ("{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE\"},", "{ \"UP / DOWN\",      \"MOVE / SCROLL / PAGE / MORE TEXT HERE\"},")):
            assert a in src, a; w = build([], src.replace(a, b, 1)); rc, _ = run(w) if w else (1, ""); print("killed  " if rc else "SURVIVED", a[:56]); bad += rc == 0
        sys.exit(1 if bad else 0)
