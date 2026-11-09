#!/usr/bin/env python3
"""Driver of sim/test_gallery_app.c: writes 18 small pictures and their index with the real converter library, builds the firmware (gallery_app.c, gallery_fmt.c, gallery_hw.c) against the hardware model and runs it.
`--mutants` edits copies of the firmware sources; every edit must make the run fail."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/gallery"))
import tgy
from PIL import Image

def make_big_set(d, n=300):
    """A 300-entry index with a thumbnail of its own for every entry (a pattern of the index) and real picture files only for the pictures the script opens (the others fail to open, which is also a case)."""
    ents, ths = [], []
    for i in range(n):
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/b{i:03d}.tgy", name=f"Photo {i:03d}", size=1748992, width=800, height=600))
        ths.append(bytes(((i * 7 + j // 192 * 5 + j % 192 * 3) & 255) for j in range(tgy.THUMB_BYTES)))
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))
    for i in (0, 150, 298, 299):
        im = Image.new("RGB", (640, 480), ((i * 5) % 256, (i * 3 + 40) % 256, (i * 11 + 90) % 256)); im.paste(((i * 13) % 256, 255, 0), (i % 200, 10, i % 200 + 100, 60))
        canvas, rect, thumb, size = tgy.render(im); (d / f"b{i:03d}.tgy").write_bytes(tgy.pack_tgy(canvas, rect, thumb, size))

def make_zoom_set(d):
    """Five pictures with zoom levels, for the zoom scenario: z00 2000 x 1500 (a raster 1:1 level), z01 1000 x 800 (raster 1:1), z02 600 x 400 (no zoom level), z03 700 x 1500 (a raster level padded to 800 wide), z04 3200 x 2400 (a
    raster half level 1600 x 1200 and a tile 1:1 level), z05 2300 x 1500 (a tile 1:1 level only: no half stop, wider than a raster level), z06 3600 x 2400 (a window placed for a pan is not clamped by the level's edges). Random pixels, so a wrong address cannot match by accident."""
    import random
    rnd = random.Random(5); ents, ths = [], []
    for i, (w, h) in enumerate([(2000, 1500), (1000, 800), (600, 400), (700, 1500), (3200, 2400), (2300, 1500), (3600, 2400)]):
        prep = tgy.prepare(Image.frombytes("RGB", (w, h), rnd.randbytes(w * h * 3)))
        canvas, rect, thumb, size = tgy.render_prepared(prep); data = tgy.pack_tgy(canvas, rect, thumb, size, tgy.zoom_levels(prep))
        (d / f"z{i:02d}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/z{i:02d}.tgy", name=f"Zoom {i:02d}", size=len(data), width=size[0], height=size[1])); ths.append(thumb)
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))

def make_set(d, n=18):
    ents, ths = [], []
    for i in range(n):
        w, h = (900, 600) if i % 3 == 0 else (400, 700) if i % 3 == 1 else (640, 640)
        im = Image.new("RGB", (w, h)); im.putdata([((x * 3 + i * 31) % 256, (y * 5 + i * 17) % 256, (x + y + i * 53) % 256) for y in range(h) for x in range(w)])
        canvas, rect, thumb, size = tgy.render(im); data = tgy.pack_tgy(canvas, rect, thumb, size)
        (d / f"p{i:02d}.tgy").write_bytes(data)
        ents.append(dict(path=f"Assets/tau_gallery/common/photos/p{i:02d}.tgy", name=f"Picture {i:02d}", size=len(data), width=size[0], height=size[1])); ths.append(thumb)
    (d / "tau-library.tdb").write_bytes(tgy.pack_index(ents, ths))

def build(workdir, fw=None):
    """Compile the test with the firmware sources from `fw` (a dict name -> text) or from the tree."""
    src = ROOT / "sim/test_gallery_app.c"; text = src.read_text()
    inc = workdir / "fw2/gallery"; inc.mkdir(parents=True, exist_ok=True)
    for sub in ("platform", "gfx", "stream"):
        (workdir / "fw2" / sub).mkdir(parents=True, exist_ok=True)
        for f in (ROOT / "fw2" / sub).glob("*.h"): (workdir / "fw2" / sub / f.name).write_text(f.read_text())
    for f in (ROOT / "fw2/cores").glob("*.h"): (workdir / "fw2/cores").mkdir(exist_ok=True); (workdir / "fw2/cores" / f.name).write_text(f.read_text())
    (workdir / "fw2/ui").mkdir(exist_ok=True); (workdir / "fw").mkdir(exist_ok=True)               # the theme module (the colour roles) and its generated data
    for n in ("theme.c", "theme.h"): (workdir / "fw2/ui" / n).write_text((ROOT / "fw2/ui" / n).read_text())
    (workdir / "fw/theme_data.h").write_text((ROOT / "fw/theme_data.h").read_text())
    for f in (ROOT / "fw2/gallery").iterdir():
        if f.suffix in (".c", ".h"): (inc / f.name).write_text((fw or {}).get(f.name, f.read_text()))
    (workdir / "sim").mkdir(exist_ok=True); (workdir / "sim/t.c").write_text(text)
    env = dict(os.environ); sdk = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "sdkroot"], capture_output=True, text=True).stdout.strip()
    if sdk: env["SDKROOT"] = sdk
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(workdir / "t"), str(workdir / "sim/t.c")], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stderr[:3000]); return False
    return True

SCENARIOS = ("resume5", "resume17", "badcrc", "toohigh", "off", "layout0", "ocean_light", "badlook")     # boot scenarios: the saved state, the grid must open on picture 6 or 18, or on picture 1 when it is not the same picture / resume is off / the file is not ours
BIGSET = None; ZSET = None
def run(workdir, setdir):
    r = subprocess.run([str(workdir / "t"), str(setdir)], capture_output=True, text=True, timeout=600)
    rc, out = r.returncode, r.stdout
    for sc in SCENARIOS:
        r = subprocess.run([str(workdir / "t"), str(setdir), sc], capture_output=True, text=True, timeout=600)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l)
    global BIGSET, ZSET
    if BIGSET is None: BIGSET = Path(tempfile.mkdtemp()); make_big_set(BIGSET)
    if ZSET is None: ZSET = Path(tempfile.mkdtemp()); make_zoom_set(ZSET)
    r = subprocess.run([str(workdir / "t"), str(ZSET), "zoom"], capture_output=True, text=True, timeout=900)
    if r.returncode: rc = 1; out += "\nFAIL scenario zoom\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l)
    for sc in ("big", "bigresume250", "bigresume299"):
        r = subprocess.run([str(workdir / "t"), str(BIGSET), sc], capture_output=True, text=True, timeout=900)
        if r.returncode: rc = 1; out += "\nFAIL scenario " + sc + "\n" + "\n".join(l for l in r.stdout.splitlines() if "FAIL" in l)
    return rc, out

def mutants(setdir):
    muts = [("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE);", "REG_WR(R_LAND_BASE, GAL_PLANE_BASE + 2u);"),
            ("gallery_app.c", "0x40000000u + c, len,", "0x40000000u, len,"),
            ("gallery_app.c", "t->canvas_off + c, 0x40000000u", "t->canvas_off, 0x40000000u"),
            ("gallery_app.c", "const uint32_t total = (pic.canvas_len + 4095u) & ~4095u;", "const uint32_t total = pic.canvas_len;"),
            ("gallery_app.c", "if (REG_RD(R_LAND_OVF) != 0u ||", "if ("),
            ("gallery_app.c", "|| REG_RD(R_LAND_WORDS) != total / 4u) return 6;", ") return 6;"),
            ("gallery_app.c", "GX0 2u", "GX0 4u") if False else ("gallery_app.c", "#define GX0 2u", "#define GX0 4u"),
            ("gallery_app.c", "#define GYS 76u", "#define GYS 75u"),
            ("gallery_app.c", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u;", "const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u + 1u;"),
            ("gallery_app.c", "if (a < off || a >= off + GX_THUMB_BYTES) continue;", "if (a < off) continue;"),
            ("gallery_app.c", "path[0] = '/';", "path[0] = 'x';"),
            ("gallery_app.c", "if (edge & KEY_B) { gal_view_off();", "if (edge & KEY_B) { "),
            ("gallery_app.c", "(sel + 1u) % head.count : (sel + head.count - 1u) % head.count;", "(sel + 1u) % head.count : (sel + head.count - 1u);"),
            ("gallery_app.c", "if (np != page) { page = np; if (!load_page())", "if (np != page) { if (!load_page())"),
            ("gallery_app.c", "else if (edge & KEY_R1) { select_to(sel + PER_PAGE < head.count ? sel + PER_PAGE : head.count - 1u); }", "else if (edge & KEY_R1) { select_to(sel + PER_PAGE < head.count ? sel + PER_PAGE : sel); }"),
            ("gallery_app.c", "gfx_text(6, 338, ent[sel - page * PER_PAGE].name, FG, BG);", "gfx_text(6, 338, ent[0].name, FG, BG);"),
            ("gallery_app.c", "if (!gal_view_on()) { say(\"VIEWER DID NOT START\", BAD); return; }", "gal_view_on();"),
            ("gallery_app.c", "if (r) { say(open_msg[r], BAD); return; }\n    if (!gal_view_on())", "if (r) { say(open_msg[r], BAD); }\n    if (!gal_view_on())"),
            ("gallery_app.c", "{ run_pages(); draw_page(); prev =", "{ run_pages(); prev ="),
            ("gallery_app.c", "if (r == GP_OPEN_CONTROLS) { gc_t c;", "if (r == GP_OPEN_HELP) { gc_t c;"),
            ("gallery_app.c", "static void page_clear(const char *title) { gfx_rect(0, 0, GFX_W, GFX_H, BG);", "static void page_clear(const char *title) { "),
            ("gallery_app.c", "draw_menu(&m);\n    }\n}", "}\n}"),
            ("gallery_app.c", "else if (edge & KEY_START) {", "else if (edge & KEY_SELECT) {"),
            ("gallery_app.c", "info.open_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);", "info.open_ms = GI_NONE;"),
            ("gallery_app.c", "if (r == 0 || r >= 5) {", "if (1) {"),
            ("gallery_app.c", "info.err = r ? open_msg[r] : 0;", "info.err = 0;"),
            ("gallery_app.c", "else if (r == GP_OPEN_HELP) {", "else if (r == GP_OPEN_INFO) {"),
            ("gallery_app.c", "info.core_version = REG_RD(R_VERSION);", "info.core_version = 0u;"),
            ("gallery_app.c", "const int r = open_timed(sel, 1);", "const int r = open_picture(sel, 1);"),
            ("gallery_app.c", "gset.last_idx = i; gset.last_crc = path_crc(i); }", "gset.last_idx = i; }"),
            ("gallery_app.c", "gs_resume_index(&gset, head.count, path_crc(gset.last_idx));", "gset.last_idx;"),
            ("gallery_app.c", "        settings_tick();\n        if (edge & KEY_LEFT) { if (sel > 0u)", "        if (edge & KEY_LEFT) { if (sel > 0u)"),
            ("gallery_app.c", "if (r == GP_OPEN_SETTINGS) { draw_settings(); while (!gsp_input(&gset.resume, next_edge(&prev))) draw_settings(); }", "if (r == GP_OPEN_SETTINGS) { draw_settings(); while (!gsp_input(&gset.resume, next_edge(&prev))) draw_settings(); gset.resume = 1u; }"),
            ("gallery_settings.c", "if (gs_publish_due(cycles(), last_activity, CLK_HZ, 1)) publish(v);", "publish(v);"),
            ("gallery_settings.c", "if (m & (1u << k)) set_wr(k, w[k]); }", "set_wr(k, w[k]); }"),
            ("gallery_settings.c", "gs_stats.layout = (w[GS_W_LAYOUT] >> 27) & 15u; gs_stats.loads++;", "gs_stats.layout = 0u; gs_stats.loads++;"),
            ("gallery_app.c", "theme_set(gset.theme, gset.pol, gset.pal); gset.theme = th_theme;", "gset.theme = th_theme;"),
            ("gallery_app.c", "gset.pol = th_pol; gset.pal = th_pal; }      /* applied at once", "gset.pol = th_pol; }      /* applied at once"),
            ("gallery_app.c", "#define BG   th_role[TR_BG_BOTTOM]", "#define BG   RGB565(0, 0, 0)"),
            ("gallery_app.c", "#define ACC  th_role[TR_ACCENT]", "#define ACC  RGB565(255, 170, 40)"),
            ("gallery_app.c", "    theme_init();\n", ""),
            ("gallery_app.c", "if (k == GT_CHANGED) { theme_set(l.theme, l.pol, l.pal);", "if (k == GT_CHANGED) { "),
            ("gallery_app.c", "if (r == GP_OPEN_THEME) {", "if (r == GP_OPEN_HELP) {"),
            ("gallery_app.c", "const uint32_t np = n / PER_PAGE;\n    if (np != page) { sel = n;", "const uint32_t np = n / (PER_PAGE + 1u);\n    if (np != page) { sel = n;"),
            ("gallery_app.c", "n_on_page = head.count - first < PER_PAGE ? head.count - first : PER_PAGE;", "n_on_page = PER_PAGE;"),
            ("gallery_app.c", "else if (edge & KEY_L1) { select_to(sel >= PER_PAGE ? sel - PER_PAGE : 0u); }", "else if (edge & KEY_L1) { select_to(sel >= PER_PAGE ? sel - PER_PAGE : sel); }"),
            ("gallery_app.c", "else if (edge & KEY_DOWN) { if (sel + COLS < head.count) select_to(sel + COLS); else if (sel + 1u < head.count) select_to(head.count - 1u); }", "else if (edge & KEY_DOWN) { if (sel + COLS < head.count) select_to(sel + COLS); }"),
            ("gallery_app.c", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;", "if (land_canvas(&pic, total, show_progress)) return 5;"),
            ("gallery_app.c", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;", "if (land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress) && land_canvas(&pic, total, show_progress)) return 5;"),
            ("gallery_app.c", "if (!read_bytes(SLOT_PIC, 0u, 4096u, h4) && !read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;", "if (!read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;"),
            ("gallery_app.c", "    info.err = r ? open_msg[r] : 0;\n", "    info.err = r ? open_msg[r] : 0;\n    if (r == 0) remember(i);\n"),
            ("gallery_app.c", "if (gv_display_y(z_wy, rlo, rhi, &y) &&", "if ((y = z_wy, rhi >= rlo + GV_SCREEN_H) &&"),
            ("gallery_app.c", "REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u)); REG_WR(R_LAND_CTRL, 1u);", "REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u));"),
            ("gallery_app.c", "len = rest < GV_CHUNK ? rest : GV_CHUNK,", "len = GV_CHUNK,"),
            ("gallery_app.c", "if (edge & (KEY_A | KEY_B)) { zoom_goto(-1); continue; }", "if (edge & KEY_A) { zoom_goto(-1); continue; }"),
            ("gallery_app.c", "REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE);              /* the fit plane", "REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16);              /* the fit plane"),
            ("gallery_app.c", "if (!((keys & KEY_SELECT) && (edge & (KEY_LEFT | KEY_RIGHT)))) {", "if (1) {"),
            ("gallery_app.c", "tick = 1; held = (dx || dy) ? held + 1u : 0u; }", "tick = 1; held = held + 1u; }"),
            ("gallery_app.c", "next_tick = cycles() + ZOOM_FRAME; held = (dx || dy) ? held + 1u : 0u;", "next_tick = cycles() + ZOOM_FRAME; held = held + 1u;"),
            ("gallery_app.c", "zoom_leave(); zoom_open(mk, sx2, sy2); return 1;", "zoom_leave(); zoom_open(0, sx2, sy2); return 1;"),
            ("gallery_app.c", "REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx, y));", "REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx & ~7, y));"),
            ("gallery_app.c", "if (!zoom_land(c)) return 1;", "if (!zoom_land(c)) { }"),
            ("gallery_app.c", "if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, diry, &c)) {", "if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, 0, &c)) {"),
            ('gallery_app.c', 'if (zoom_pass(dx, dy, held, tick && (dx || dy), tick)) {', 'if (zoom_pass(dx, dy, held, (dx || dy), tick)) {'),
            ("gallery_app.c", "if (t_recentre && gv_win_tile(t_wx[t_front], t_wy[t_front], tx, ty, &fc, &fr) && t_res[t_front][fr * GV_WIN_TX + fc]) {", "if (0 && gv_win_tile(t_wx[t_front], t_wy[t_front], tx, ty, &fc, &fr) && t_res[t_front][fr * GV_WIN_TX + fc]) {"),
            ('gallery_app.c', 'if (!z_shown) { if (gv_screen_ready(&z_stop, z_wx, z_wy, t_wx[t_front], t_wy[t_front], t_res[t_front])) tile_show(z_wx, z_wy); return 0; }', 'if (!z_shown) { tile_show(z_wx, z_wy); return 0; }'),
            ("gallery_app.c", "t_dst_hw = win_base(tw) + gv_tile_hw(col, row);", "t_dst_hw = win_base(t_front) + gv_tile_hw(col, row);"),
            ("gallery_app.c", "#define COPY_PER_PASS 64u", "#define COPY_PER_PASS 1000u"),
            ('gallery_app.c', 'else if (!gv_window_ok(&z_stop, t_wx[b], t_wy[b], px, py)) { gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); }', 'else if (0) { }'),
            ('gallery_app.c', 'if (gv_window_ok(&z_stop, t_wx[t_front], t_wy[t_front], px, py)) t_recentre = 0u;', 'if (0) t_recentre = 0u;'),
            ("gallery_app.c", "gv_map_corner(sf, fox, foy, zoom_scale(k), tox, toy, x, y, &nx, &ny);", "gv_map_corner(sf, fox, foy, zoom_scale(k), tox, toy, 0, 0, &nx, &ny);"),
            ("gallery_app.c", "if (zn > 0u) { zoom_goto((int)zn - 1);", "if (zn > 0u) { zoom_goto(0);"),
            ("gallery_app.c", "    const uint32_t tc = cycles();\n    for (int attempt = 0; attempt < 2; attempt++) {\n        REG_WR(R_LAND_BASE, GAL_STAGE);", "    const uint32_t tc = cycles();\n    for (int attempt = 0; attempt < 1; attempt++) {\n        REG_WR(R_LAND_BASE, GAL_STAGE);"),
            ('gallery_app.c', '(z_shown ? gv_screen_ready(&z_stop, z_sx, z_sy, t_wx[b2], t_wy[b2], t_res[b2]) : gv_screen_ready(&z_stop, px, py, t_wx[b2], t_wy[b2], t_res[b2]))', 'gv_window_ok(&z_stop, t_wx[b2], t_wy[b2], px, py)'),
            ('gallery_app.c', 'if (z_shown) { px = z_sx + clampi(z_wx - z_sx, -SLACK, SLACK); py = z_sy + clampi(z_wy - z_sy, -SLACK, SLACK); gv_clamp(&z_stop, &px, &py); }', 'if (z_shown) { }'),
            ('gallery_app.c', 'const int32_t cx = (t_recentre && z_shown) ? z_sx : px, cy = (t_recentre && z_shown) ? z_sy : py;', 'const int32_t cx = px, cy = py;'),
            ('gallery_app.c', 'if (t_front != t_shown_win) { tile_show(z_sx, z_sy); return 0; }', 'if (t_front != t_shown_win) { return 0; }'),
            ('gallery_app.c', 'if (tick && (z_sx != z_wx || z_sy != z_wy)) {', 'if (0) {'),
            ('gallery_app.c', '#define FOLLOW_STEP 16', '#define FOLLOW_STEP 64'),
            ("gallery_app.c", "if (edge & KEY_L1) { zoom_goto(zk > 0 ? zk - 1 : -1);", "if (edge & KEY_L1) { zoom_goto(zk > 0 ? zk - 1 : 0);"),
            ("gallery_app.c", "info.land_ms = (cycles() - tc) / (CLK_HZ / 1000u); return 1; }\n    }\n    return 0;\n}\n/* One pass of the raster", "info.land_ms = (cycles() - tc) / (CLK_HZ / 100u); return 1; }\n    }\n    return 0;\n}\n/* One pass of the raster"),
            ("gallery_app.c", "t_copy_cyc += cycles() - tc; info.copy_ms = t_copy_cyc / (CLK_HZ / 1000u); } else t_copy_cyc += cycles() - tc;", "info.copy_ms = (cycles() - tc) / (CLK_HZ / 1000u); } else t_copy_cyc += cycles() - tc;"),
            ("gallery_app.c", "if (z_first) { z_first = 0u; info.zoom_ms = (cycles() - z_t0) / (CLK_HZ / 1000u); }", "if (z_first) { z_first = 0u; info.zoom_ms = 0u; }"),
            ("gallery_app.c", "if (w) gfx_rect(LDR_X, LDR_Y, w > LDR_W ? LDR_W : w, LDR_H, ACC);", "if (w) gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, ACC);"),
            ("gallery_app.c", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, LDR_TRACK);\n", "\n"),
            ("gallery_app.c", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, LDR_TRACK);", "gfx_rect(LDR_X, LDR_Y, LDR_W, LDR_H, DIM);"),
            ("gallery_app.c", "#define LDR_H 2u", "#define LDR_H 4u"),
            ("gallery_app.c", "*q = 0; say(b, ACC); loader_bar(c + len, total); }", "*q = 0; say(b, ACC); }"),
            ("gallery_app.c", 'if (show_progress) { say("OPENING", ACC); loader_bar(0u, 1u); }', 'if (show_progress) { say("OPENING", ACC); }'),
            ("gallery_app.c", "loader_bar(c + len, total); }", "loader_bar(c, total); }"),
            ("gallery_app.c", "#define LDR_Y 178u", "#define LDR_Y 200u"),
            ("gallery_app.c", "gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); t_recentre = 1u;", "gv_window_lead(&z_stop, px, py, 0, 0, &t_wx[b], &t_wy[b]); res_clear(b); t_recentre = 1u;"),
            ("gallery_app.c", "gv_window_lead(&z_stop, px, py, lx, ly, &t_wx[b], &t_wy[b]); res_clear(b); }", "gv_window_lead(&z_stop, px, py, 0, 0, &t_wx[b], &t_wy[b]); res_clear(b); }"),
            ("gallery_app.c", "const int lx = dx ? dx : (z_wx > px ? 1 : z_wx < px ? -1 : 0), ly = dy ? dy : (z_wy > py ? 1 : z_wy < py ? -1 : 0);", "const int lx = -dx, ly = -dy;"),
            ("gallery_app.c", "const int lx = dx ? dx : (z_wx > px ? 1 : z_wx < px ? -1 : 0), ly = dy ? dy : (z_wy > py ? 1 : z_wy < py ? -1 : 0);", "const int lx = dx, ly = dy;"),
            ("gallery_app.c", "    zb_end();                                                                                /* the fit plane's own pixels back under the loader bar */\n", "\n"),
            ("gallery_app.c", "static void zoom_shown_now(void) { zb_end(); if (z_first) {", "static void zoom_shown_now(void) { if (z_first) {"),
            ("gallery_app.c", "    zb_begin();\n}", "}"),
            ("gallery_app.c", "static void zb_begin(void) { if (zb_on) return; zb_copy(0); zb_on = 1u;", "static void zb_begin(void) { if (zb_on) return; zb_on = 1u;"),
            ("gallery_app.c", "zb_write(0u, 160u, 0u); }", "}"),
            ("gallery_app.c", "if (px > zb_px) { zb_write(zb_px, px, px); zb_px = px; }", "if (px > zb_px) { zb_px = px; }"),
            ("gallery_app.c", "zb_set(whole, on);", "zb_set(0u, on);"),
            ("gallery_app.c", "zb_set(z_hi - z_lo, need); }", "zb_set(0u, need); }"),
            ("gallery_app.c", "#define ZB_Y 358u", "#define ZB_Y 300u"),
            ("gallery_app.c", "#define ZB_HW0 480u ", "#define ZB_HW0 482u "),
            ("gallery_app.c", "const uint32_t o = k * (ZB_HW / 2u), save = GAL_BARSAVE + r * ZB_HW;", "const uint32_t o = k * (ZB_HW / 2u), save = GAL_BARSAVE;"),
            ("gallery_app.c", "const uint16_t c = px < fillpx ? ac : tr;", "const uint16_t c = px < fillpx ? tr : ac;") if False else ("gallery_app.c", "px < fillpx ? ac : tr;", "px < fillpx ? tr : tr;"),
            ("gallery_app.c", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lx, ly, need)", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], 0, 0, need)"),
            ("gallery_app.c", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lx, ly, need)", "gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], dx, dy, need)"),
            ("gallery_app.c", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); zoom_reshow(); return 0; }", "{ gfx_fence(); zoom_reshow(); return 0; }"),
            ("gallery_app.c", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); zoom_reshow(); return 0; }", "{ map_ops(2, dx0, dy0, dw0, dh0); gfx_fence(); return 0; }"),
            ("gallery_app.c", "        else if (op == 1) gfx_blit_raw(GAL_BARSAVE, pb, MAP_SPR + d, poff + d, w);", "        else if (op == 1) { }"),
            ("gallery_app.c", "        if (op == 0) gfx_blit_raw(pb, GAL_BARSAVE, poff + d, MAP_SAVE + soff + d, w);", "        if (op == 0) { }"),
            ("gallery_app.c", "map_ops(2, m_rx, m_ry, m_rw, m_rh)", "map_ops(2, m_rx, m_ry, m_rw, m_rh)") if False else ("gallery_app.c", "map_ops(2, dx0, dy0, dw0, dh0); map_ops(0, m_rx, m_ry, m_rw, m_rh);", "map_ops(0, m_rx, m_ry, m_rw, m_rh);"),
            ("gallery_app.c", "if (dx || dy) { const int32_t st = gv_speed(held); m_rx += dx * st; m_ry += dy * st; gv_map_clamp((int32_t)pic.rx, (int32_t)pic.ry, (int32_t)pic.rw, (int32_t)pic.rh, m_rw, m_rh, &m_rx, &m_ry); redraw = 1; }", "if (dx || dy) { const int32_t st = gv_speed(held); m_rx += dx * st; m_ry += dy * st; redraw = 1; }"),
            ("gallery_app.c", "if ((edge & KEY_L1) && mk > 0) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk--; map_rect_of(mk, cx, cy); redraw = 1; }", "if ((edge & KEY_L1) && mk > 0) { }"),
            ("gallery_app.c", "if ((edge & KEY_R1) && (uint32_t)mk + 1u < zn) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk++; map_rect_of(mk, cx, cy); redraw = 1; }", "if ((edge & KEY_R1) && (uint32_t)mk + 1u < zn) { const int32_t cx = m_rx + m_rw / 2, cy = m_ry + m_rh / 2; mk++; redraw = 1; }"),
            ("gallery_app.c", "m_rw = rw; m_rh = rh; m_rx = cx - rw / 2; m_ry = cy - rh / 2;", "m_rw = rw; m_rh = rh; m_rx = cx; m_ry = cy;"),
            ("gallery_app.c", "if ((edge & KEY_START) && z_shown) {", "if (edge & KEY_START) {"),
            ("gallery_app.c", "if ((edge & KEY_START) && z_shown) { map_mode();", "if (0 && (edge & KEY_START) && z_shown) { map_mode();"),
            ("gallery_app.c", "const int32_t px = side ? rx + rw - GV_RECT_T : rx;", "const int32_t px = side ? rx + rw - GV_RECT_T - 2 : rx;"),
            ("gallery_app.c", "const int32_t row = e < 2 ? ry + e : ry + rh - 4 + e;", "const int32_t row = e < 2 ? ry + e : ry + rh - 3 + e;"),
            ("gallery_app.c", "info.flip_ms = (cycles() - t_rc_t0) / (CLK_HZ / 1000u);", "info.flip_ms = 0u;"),
            ("gallery_fmt.c", "return h->entries_off + i * GX_ENTRY_BYTES;" if False else "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * GX_THUMB_BYTES; }", "uint32_t gx_thumb_off(const gx_head_t *h, uint32_t thumb) { return h->atlas_off + thumb * (GX_THUMB_BYTES - 2u); }")]
    bad = 0
    for i, (f, a, b) in enumerate(muts):
        text = (ROOT / "fw2/gallery" / f).read_text(); assert a in text, (i, a)
        w = Path(tempfile.mkdtemp())
        if not build(w, {f: text.replace(a, b, 1)}): print("killed   (does not build)", i); continue
        rc, out = run(w, setdir); ok = rc == 0 and "PASSED" in out
        print("SURVIVED" if ok else "killed  ", i, a[:60]); bad += ok
    return bad

if __name__ == "__main__":
    setdir = Path(tempfile.mkdtemp()); make_set(setdir)
    if "--mutants" in sys.argv: sys.exit(1 if mutants(setdir) else 0)
    w = Path(tempfile.mkdtemp())
    if not build(w): sys.exit(1)
    rc, out = run(w, setdir); print("\n".join(l for l in out.splitlines() if "FAIL" in l or "PASSED" in l)); sys.exit(rc)
