#pragma GCC optimize ("Os")
/* Tau Gallery M1: the first gallery (docs/M1_DESIGN.md). A 4 x 4 grid of 96 x 72 thumbnails on the 400 x 360 UI; A opens the selected picture: its 800 x 720 fit canvas (one contiguous run of the .tgy) lands in
 * the viewer plane through the landing path, then the viewer shows it. In the viewer, Left and Right go to the previous and next picture (the new one paints top to bottom as it lands) and B returns to the grid.
 * Data: the folder index tau-library.tdb is data slot 5 (TGIX, tools/gallery/tgy.py), the pictures are opened by path on slot 2. Build: APP=gallery bash fw2/build_gallery.sh. */
#include "../platform/platform.h"
#include "../gfx/gfx.h"
#include "../stream/slot.h"
#include "gallery_hw.h"
#include "gallery_fmt.h"
#include "gallery_pages.h"
#include "gallery_settings_hw.h"
#include "../ui/theme.h"
#include "gallery_view.h"

#define BG   th_role[TR_BG_BOTTOM]                /* the colours are the theme's roles (ui/theme.c, docs/THEME_SPEC.md) */
#define FG   th_role[TR_TEXT_PRIMARY]
#define DIM  th_role[TR_TEXT_SECONDARY]
#define ACC  th_role[TR_ACCENT]
#define BAD  th_role[TR_ERROR]
#define COLS 4u
#define ROWS 4u
#define PER_PAGE (COLS * ROWS)
#define GX0 2u
#define GXS 100u
#define GY0 22u
#define GYS 76u
#define SLOT_PIC 2u
#define SLOT_IDX 5u
#define GAL_ZOOM_BASE 0x400000u                  /* the zoom plane (a raster zoom level at its own stride), SDRAM halfword address 8 MiB */
#define ZOOM_FRAME (CLK_HZ / 60u)                /* one pan step a frame */
#define LAND_CHUNK 131072u                       /* the largest single command measured in M0 (1.5 to 1.6 MB/s) */


static char *put_u(char *q, uint32_t v) { char t[10]; uint32_t n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *q++ = t[--n]; return q; }
static char *put_s(char *q, const char *p) { while (*p) *q++ = *p++; return q; }

/* Reads len bytes at byte offset off of a data slot into dst, through 4 KB aligned commands into the tag zone (the slot files are padded to 4 KB). Returns 1 on success. */
static int read_bytes(uint32_t slot, uint32_t off, uint32_t len, uint8_t *dst)
{
    uint32_t pos = off & ~4095u;
    while (pos < off + len) {
        if (!slot_read_blocking(slot, pos, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) return 0;
        const volatile uint32_t *w = GAL_TAGWIN;
        for (uint32_t i = 0; i < 1024u; i++) {
            const uint32_t b = pos + 4u * i, v = w[i];
            for (uint32_t k = 0; k < 4u; k++) { const uint32_t a = b + k; if (a >= off && a < off + len) dst[a - off] = (uint8_t)(v >> (8u * k)); }
        }
        pos += 4096u;
    }
    return 1;
}

static gx_head_t head; static gx_entry_t ent[PER_PAGE];
static uint32_t sel, page, loaded_page = 0xFFFFFFFFu, n_on_page;

static void say(const char *s, uint16_t color) { gfx_rect(0, 336, GFX_W, 24, BG); gfx_text(6, 338, s, color, BG); }
static void cell_xy(uint32_t k, uint32_t *x, uint32_t *y) { *x = GX0 + (k % COLS) * GXS; *y = GY0 + (k / COLS) * GYS; }
static void frame(uint32_t k, uint16_t c)
{
    uint32_t x, y; cell_xy(k, &x, &y);
    gfx_rect(x - 2u, y - 2u, 100u, 2u, c); gfx_rect(x - 2u, y + 72u, 100u, 2u, c); gfx_rect(x - 2u, y, 2u, 72u, c); gfx_rect(x + 96u, y, 2u, 72u, c);
}

/* One thumbnail: its 13,824 bytes are read in 4 KB pieces and written into the framebuffer two pixels at a time (the mailbox), 96 pixels = 192 bytes a row. */
static int draw_thumb(uint32_t x, uint32_t y, uint32_t off)
{
    for (uint32_t pos = off & ~4095u; pos < off + GX_THUMB_BYTES; pos += 4096u) {
        if (!slot_read_blocking(SLOT_IDX, pos, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) return 0;
        const volatile uint32_t *w = GAL_TAGWIN;
        for (uint32_t i = 0; i < 1024u; i++) {
            const uint32_t a = pos + 4u * i;
            if (a < off || a >= off + GX_THUMB_BYTES) continue;
            const uint32_t rel = a - off, row = rel / 192u, col = (rel % 192u) / 2u;
            gfx_mb_write_buf((y + row) * GFX_STRIDE + x + col, w[i]);
        }
    }
    return 1;
}

static int load_page(void)
{
    const uint32_t first = page * PER_PAGE; n_on_page = head.count - first < PER_PAGE ? head.count - first : PER_PAGE;
    static uint8_t buf[PER_PAGE * GX_ENTRY_BYTES];
    if (!read_bytes(SLOT_IDX, gx_entry_off(&head, first), n_on_page * GX_ENTRY_BYTES, buf)) return 0;
    for (uint32_t k = 0; k < n_on_page; k++) if (gx_parse_entry(buf + k * GX_ENTRY_BYTES, GX_ENTRY_BYTES, &ent[k])) return 0;
    loaded_page = page; return 1;
}

static void footer(void)
{
    char b[48], *q = put_s(b, ""); q = put_u(q, sel + 1u); q = put_s(q, "/"); q = put_u(q, head.count); *q = 0;
    gfx_rect(0, 336, GFX_W, 24, BG);
    gfx_text(6, 338, ent[sel - page * PER_PAGE].name, FG, BG);
    gfx_text(GFX_W - 16u - gfx_text_width(b), 338, b, DIM, BG);   /* gfx_text drops a glyph cell that starts within 16 px of the right edge: a 6 px margin lost the last digit */
}

static void draw_page(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(6, 2, "TAU GALLERY", ACC, BG);
    if (loaded_page != page && !load_page()) { say("INDEX READ FAILED", BAD); return; }
    for (uint32_t k = 0; k < n_on_page; k++) {
        uint32_t x, y; cell_xy(k, &x, &y);
        if (!draw_thumb(x, y, gx_thumb_off(&head, ent[k].thumb))) { say("THUMBNAIL READ FAILED", BAD); return; }
    }
    frame(sel - page * PER_PAGE, ACC); footer();
}

static void select_to(uint32_t n)
{
    if (n >= head.count) return;
    const uint32_t np = n / PER_PAGE;
    if (np != page) { sel = n; page = np; draw_page(); return; }
    frame(sel - page * PER_PAGE, BG); sel = n; frame(sel - page * PER_PAGE, ACC); footer();
}

/* Lands the picture's fit canvas in the viewer plane. Returns 0 when it is there, else a code for the status line. */
static uint8_t h4[4096];                                                       /* the first 4 KB of the open picture: its header and level table */
/* One landing of the canvas into the plane (restarts the landing counters). Returns 0, or 5 when a card read failed. */
static int land_canvas(const gt_info_t *t, uint32_t total, int show_progress)
{
    REG_WR(R_LAND_BASE, GAL_PLANE_BASE); REG_WR(R_LAND_CTRL, 1u);
    for (uint32_t c = 0; c < total; c += LAND_CHUNK) {
        const uint32_t len = total - c < LAND_CHUNK ? total - c : LAND_CHUNK;
        if (!slot_read_blocking(SLOT_PIC, t->canvas_off + c, 0x40000000u + c, len, CLK_HZ * 4u)) return 5;
        if (show_progress) { char b[24], *q = put_s(b, "LOADING "); q = put_u(q, (c + len) * 100u / total); q = put_s(q, "%"); *q = 0; say(b, ACC); }
    }
    return 0;
}
static int open_picture(uint32_t i, int show_progress)
{
    char path[GX_PATH_MAX + 2u]; path[0] = '/';
    { const char *s = ent[i - page * PER_PAGE].path; uint32_t n = 0; while (s[n] && n < GX_PATH_MAX) { path[1u + n] = s[n]; n++; } path[1u + n] = 0; }
    if (show_progress) say("OPENING", ACC);
    if (!slot_open_path(SLOT_PIC, path)) return 1;
    gt_info_t t;
    if (!read_bytes(SLOT_PIC, 0u, 4096u, h4) && !read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;      /* one retry of a failed read: a transient card error should not cost the picture */
    if (gt_parse(h4, 4096u, &t)) return 3;
    if (!(REG_RD(R_LAND_STATUS) >> 31)) return 4;                                   /* no landing path in this bitstream */
    const uint32_t total = (t.canvas_len + 4095u) & ~4095u;                         /* the file is padded to 4 KB: read the padding too, it lands just past the plane */
    if (land_canvas(&t, total, show_progress) && land_canvas(&t, total, show_progress)) return 5;     /* a failed read lands the whole picture again once (the counters restart with it); two failures are reported */
    uint32_t t0 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
    if (REG_RD(R_LAND_OVF) != 0u || REG_RD(R_LAND_WORDS) != total / 4u) return 6;   /* a word was dropped or lost: do not show a damaged picture */
    return 0;
}
static const char *const open_msg[] = { "", "OPEN FAILED", "HEADER READ FAILED", "NOT A TGY PICTURE", "NO LANDING PATH", "PICTURE READ FAILED", "PICTURE DAMAGED IN TRANSFER" };

/* open_picture with the Info page's numbers: how long it took (ms), the landing's word counts (only when a landing ran), the status line text. */
static gi_t info = { 0u, 0u, 0u, GI_NONE, 0u, 0u, 0, 0u, 0u, 0u };
static gs_vals_t gset; static uint32_t last_act;                                              /* the persisted settings; the clock of the last key or card read (a save waits for a quiet second after it) */
static uint32_t path_crc(uint32_t i) { const char *s = ent[i - page * PER_PAGE].path; uint32_t n = 0; while (s[n]) n++; return gf_crc32((const uint8_t *)s, n) & GS_M31; }
static void remember(uint32_t i) { gset.last_idx = i; gset.last_crc = path_crc(i); }       /* the picture the next start resumes on: only one the viewer has shown */
static void settings_tick(void) { gs_hw_service(&gset, last_act); }
static int open_timed(uint32_t i, int show_progress)
{
    const uint32_t t0 = cycles();
    const int r = open_picture(i, show_progress);
    info.open_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);
    if (r == 0 || r >= 5) { info.words = REG_RD(R_LAND_WORDS); info.lost = REG_RD(R_LAND_OVF); } else { info.words = 0u; info.lost = 0u; }
    info.err = r ? open_msg[r] : 0;
    last_act = cycles();
    return r;
}

/* ---- M2 zoom, option A (docs/M2_DESIGN.md sections 3 and 4): a raster zoom level is landed in 48 KB chunks into the zoom plane at its own stride; A toggles it, the D-pad pans, B returns to the fit view.
 * The screen is shown only inside the rows that have wholly landed (gallery_view.h); one chunk lands a pass, so the keys are polled between commands. ---- */
static gt_zoom_t zl; static gv_stop_t z_stop;
static uint32_t z_stride_hw, z_nch, z_lo, z_hi, z_shown;        /* the level's row stride in halfwords, its chunks, the loaded chunk interval, 1 when the zoom plane is on screen */
static int32_t z_wx, z_wy, z_sx, z_sy;                          /* the wanted and the shown top-left corner */
static int zoom_find(void)
{
    for (uint32_t k = 0; k < 8u; k++) { gt_zoom_t z; if (gt_zoom_level(h4, 4096u, k, &z)) return 0; if (z.layout == 0u) { zl = z; return 1; } }
    return 0;
}
static int zoom_enter(void)
{
    if (!zoom_find()) return 0;
    z_stride_hw = zl.stride / 2u; z_nch = gv_nchunks((zl.len + 4095u) & ~4095u); z_lo = 0u; z_hi = 0u; z_shown = 0u;
    z_stop.w = zl.w; z_stop.h = zl.h; z_stop.scale_q16 = zl.scale_q16;
    z_wx = ((int32_t)zl.w - GV_SCREEN_W) / 2; z_wy = ((int32_t)zl.h - GV_SCREEN_H) / 2; gv_clamp(&z_stop, &z_wx, &z_wy);
    z_sx = -1; z_sy = -1; return 1;
}
static void zoom_leave(void) { REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE); z_shown = 0u; }      /* the fit plane is still in SDRAM: back in one write */
/* Lands chunk c of the level (a read of the file's own bytes, at the same offset of the zoom plane), once more on a failure. 1 when it landed with every word counted. */
static int zoom_land(uint32_t c)
{
    const uint32_t padded = (zl.len + 4095u) & ~4095u, rest = padded - c * GV_CHUNK, len = rest < GV_CHUNK ? rest : GV_CHUNK;
    for (int attempt = 0; attempt < 2; attempt++) {
        REG_WR(R_LAND_BASE, GAL_ZOOM_BASE + c * (GV_CHUNK / 2u)); REG_WR(R_LAND_CTRL, 1u);
        if (!slot_read_blocking(SLOT_PIC, zl.off + c * GV_CHUNK, 0x40000000u, len, CLK_HZ)) continue;
        const uint32_t t0 = cycles(); while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
        if (REG_RD(R_LAND_OVF) == 0u && REG_RD(R_LAND_WORDS) == len / 4u) return 1;
    }
    return 0;
}
/* One pass of the zoom loop: on a frame tick the held direction moves the wanted corner; one chunk lands; the screen follows inside the loaded rows. Returns 1 when a chunk could not be read. */
static int zoom_pass(int dirx, int diry, uint32_t held, int moving)
{
    if (moving) gv_move(&z_stop, &z_wx, &z_wy, dirx, diry, held);
    uint32_t c;
    if (gv_next_chunk(zl.stride, zl.len, z_nch, z_lo, z_hi, (uint32_t)z_wy, diry, &c)) {
        if (!zoom_land(c)) return 1;
        if (z_hi <= z_lo) { z_lo = c; z_hi = c + 1u; } else if (c + 1u == z_lo) z_lo = c; else z_hi++;
    }
    uint32_t rlo, rhi; int32_t y;
    gv_rows_loaded(zl.stride, zl.len, z_lo, z_hi, &rlo, &rhi);
    if (gv_display_y(z_wy, rlo, rhi, &y) && (!z_shown || y != z_sy || z_wx != z_sx)) {
        REG_WR(R_VIEW_STRIDE, z_stride_hw); REG_WR(R_VIEW_BASE, gv_plane_base(GAL_ZOOM_BASE, z_stride_hw, z_wx, y)); z_sx = z_wx; z_sy = y; z_shown = 1u;
    }
    return 0;
}

static void viewer(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu, held = 0u, next_tick = 0u; int zoomed = 0;
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) last_act = cycles();
        settings_tick();
        if (zoomed) {
            if (edge & (KEY_A | KEY_B)) { zoom_leave(); zoomed = 0; continue; }                      /* A: back to fit; B: back one level, not to the grid */
            if (!((keys & KEY_SELECT) && (edge & (KEY_LEFT | KEY_RIGHT)))) {                          /* Select + Left/Right leave the zoom and go to the next picture below; anything else pans */
                const int dx = (keys & KEY_RIGHT) ? 1 : (keys & KEY_LEFT) ? -1 : 0, dy = (keys & KEY_DOWN) ? 1 : (keys & KEY_UP) ? -1 : 0;
                int tick = 0; if ((int32_t)(cycles() - next_tick) >= 0) { next_tick = cycles() + ZOOM_FRAME; tick = 1; held = (dx || dy) ? held + 1u : 0u; }
                if (zoom_pass(dx, dy, held, tick && (dx || dy))) { zoom_leave(); gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say("PICTURE READ FAILED", BAD); return; }
                continue;
            }
            zoom_leave(); zoomed = 0;
        }
        if (edge & KEY_A) { if (zoom_enter()) { zoomed = 1; held = 0u; next_tick = cycles(); } continue; }
        if (edge & KEY_B) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); return; }
        if (edge & (KEY_LEFT | KEY_RIGHT)) {
            const uint32_t n = (edge & KEY_RIGHT) ? (sel + 1u) % head.count : (sel + head.count - 1u) % head.count;
            const uint32_t np = n / PER_PAGE;
            if (np != page) { page = np; if (!load_page()) { gal_view_off(); draw_page(); return; } }
            sel = n;
            const int r = open_timed(sel, 0);
            if (r) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say(open_msg[r], BAD); return; }
            remember(sel);
        }
    }
}

static void open_from_grid(void)
{
    const int r = open_timed(sel, 1);
    if (r) { say(open_msg[r], BAD); return; }
    if (!gal_view_on()) { say("VIEWER DID NOT START", BAD); return; }
    remember(sel);
    viewer();
}

/* ---- the menu and its pages (gallery_pages.h): Start on the grid opens them, B or Start closes the menu and the grid is drawn again ---- */
#define LINE_H 20u
#define PAGE_Y0 26u
static void page_clear(const char *title) { gfx_rect(0, 0, GFX_W, GFX_H, BG); gfx_text(6, 2, title, ACC, BG); }
static void page_foot(const char *left, uint32_t n, uint32_t of)
{
    char b[24], *q = put_u(b, n); q = put_s(q, "/"); q = put_u(q, of); *q = 0;
    gfx_rect(0, 336, GFX_W, 24, BG); gfx_text(6, 338, left, DIM, BG);
    if (of) gfx_text(GFX_W - 16u - gfx_text_width(b), 338, b, DIM, BG);
}
static void draw_menu(const gm_t *m)
{
    page_clear("TAU GALLERY");
    for (uint32_t i = 0; i < GM_COUNT; i++) {
        gfx_text(6, PAGE_Y0 + i * LINE_H, gm_names[i], i == m->sel ? FG : DIM, BG);
        if (i == m->sel) gfx_text(GFX_W - 32u, PAGE_Y0 + i * LINE_H, "<", ACC, BG);
    }
    page_foot("A OPEN   B CLOSE", 0, 0);
}
static void draw_controls(const gc_t *c)
{
    page_clear("CONTROLS");
    for (uint32_t r = 0; r < GC_VIS && c->top + r < GC_N; r++) {
        const gc_row_t *w = &gc_rows[c->top + r]; const uint32_t y = PAGE_Y0 + r * (LINE_H - 2u);
        if (!w->what[0]) { gfx_text(6, y, w->key, ACC, BG); continue; }
        gfx_text(14, y, w->key, c->top + r == c->sel ? ACC : FG, BG); gfx_text(190, y, w->what, DIM, BG);
    }
    page_foot("UP DOWN SCROLL   B BACK", c->sel + 1u, GC_N);
}
static void draw_help(const gh_t *h)
{
    const gh_page_t *pg = &gh_pages[h->page];
    page_clear(pg->title);
    for (uint32_t i = 0; i < pg->n; i++) {
        const gh_line_t *l = &pg->lines[i];
        if (l->style != GS_GAP) gfx_text(6, PAGE_Y0 + i * (LINE_H - 2u), l->s, l->style == GS_ACCENT ? ACC : l->style == GS_DIM ? DIM : FG, BG);
    }
    page_foot("UP DOWN PAGE   B BACK", h->page + 1u, GH_PAGES);
}
static void draw_theme(const gt_t *g, const gt_look_t *l)
{
    page_clear("THEME");
    for (uint32_t r = 0; r < GT_ROWS; r++) {
        const uint32_t y = PAGE_Y0 + r * LINE_H; const char *v = r == GT_THEME ? theme_name(l->theme) : r == GT_MODE ? (l->pol ? "LIGHT" : "DARK") : th_palette_name[l->pal];
        gfx_text(6, y, gt_labels[r], r == g->sel ? FG : DIM, BG); gfx_text(110, y, v, r == g->sel ? FG : DIM, BG);
        if (r == g->sel) gfx_text(GFX_W - 32u, y, "<", ACC, BG);
    }
    gfx_rect(110, PAGE_Y0 + 3u * LINE_H + 6u, 120, 14, ACC);                                       /* the accent as it is drawn with this theme */
    page_foot("UP DOWN ROW  LEFT RIGHT CHANGE  B BACK", 0, 0);
}
static void draw_info(void)
{
    gi_row_t r[GI_ROWS];
    info.core_version = REG_RD(R_VERSION); info.count = head.count; info.view_caps = REG_RD(R_VIEW_CAPS);
    info.set_layout = gs_stats.layout; info.set_loads = gs_stats.loads; info.set_stores = gs_stats.stores;
    gi_format(&info, r); page_clear("INFO");
    for (uint32_t i = 0; i < GI_ROWS; i++) { gfx_text(6, PAGE_Y0 + i * LINE_H, r[i].label, DIM, BG); gfx_text(150, PAGE_Y0 + i * LINE_H, r[i].value, i == 5u && info.err ? BAD : FG, BG); }
    page_foot("B BACK", 0, 0);
}
static void draw_settings(void)
{
    page_clear("SETTINGS");
    gfx_text(6, PAGE_Y0, "RESUME LAST PICTURE", FG, BG); gfx_text(250, PAGE_Y0, gset.resume ? "ON" : "OFF", ACC, BG);
    gfx_text(6, PAGE_Y0 + 2u * LINE_H, "ON: the grid opens on the last", DIM, BG); gfx_text(6, PAGE_Y0 + 3u * LINE_H - 2u, "picture you looked at.", DIM, BG);
    page_foot("LEFT RIGHT CHANGE   B BACK", 0, 0);
}
/* Waits for the next key press (an edge) and returns it. */
static uint32_t next_edge(uint32_t *prev)
{
    for (;;) { const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~*prev; *prev = keys; if (edge) { last_act = cycles(); return edge; } settings_tick(); }
}
static void run_pages(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu;
    gm_t m; gm_open(&m); draw_menu(&m);
    for (;;) {
        const uint32_t edge = next_edge(&prev); const int r = gm_input(&m, edge);
        if (r == GP_CLOSE) return;
        if (r == GP_STAY) { draw_menu(&m); continue; }
        if (r == GP_OPEN_THEME) {
            gt_t g; gt_open(&g); gt_look_t l = { th_theme, th_pol, th_pal }; draw_theme(&g, &l);
            for (;;) {
                const int k = gt_input(&g, &l, theme_count(), next_edge(&prev));
                if (k == GT_CLOSE) break;
                if (k == GT_CHANGED) { theme_set(l.theme, l.pol, l.pal); gset.theme = th_theme; gset.pol = th_pol; gset.pal = th_pal; }      /* applied at once; saved after a quiet second */
                draw_theme(&g, &l);
            }
        }
        else if (r == GP_OPEN_SETTINGS) { draw_settings(); while (!gsp_input(&gset.resume, next_edge(&prev))) draw_settings(); }
        else if (r == GP_OPEN_CONTROLS) { gc_t c; gc_open(&c); draw_controls(&c); while (!gc_input(&c, next_edge(&prev))) draw_controls(&c); }
        else if (r == GP_OPEN_HELP) { gh_t h; gh_open(&h); draw_help(&h); while (!gh_input(&h, next_edge(&prev))) draw_help(&h); }
        else { draw_info(); while (!gi_input(next_edge(&prev))) draw_info(); }
        draw_menu(&m);
    }
}

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    theme_init();
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    platform_probe();
    gfx_text(6, 2, "TAU GALLERY", ACC, BG);
    { static uint8_t h[4096]; int rc = 100;
      if (read_bytes(SLOT_IDX, 0u, 4096u, h)) rc = gx_parse_head(h, GX_HEAD_BYTES, &head);
      if (rc == 100) { say("NO INDEX: TAU-LIBRARY.TDB NOT READ", BAD); for (;;) { } }
      if (rc) { char b[40], *q = put_s(b, "INDEX NOT USABLE E"); q = put_u(q, (uint32_t)rc); *q = 0; say(b, BAD); for (;;) { } }
      if (head.count == 0u) { say("NO PICTURES IN THE INDEX", BAD); for (;;) { } } }
    gs_hw_load(&gset); last_act = cycles();
    theme_set(gset.theme, gset.pol, gset.pal); gset.theme = th_theme; gset.pol = th_pol; gset.pal = th_pal;      /* the saved look (a value this build does not have falls back to the default) */
    if (gset.resume && gset.last_idx < head.count) {                                           /* resume: only when it is the same picture (index and path CRC) */
        page = gset.last_idx / PER_PAGE;
        if (load_page()) { const uint32_t i = gs_resume_index(&gset, head.count, path_crc(gset.last_idx)); sel = i; page = i / PER_PAGE; }
        else page = 0u;
    }
    draw_page();
    uint32_t prev = 0;
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) last_act = cycles();
        settings_tick();
        if (edge & KEY_LEFT) { if (sel > 0u) select_to(sel - 1u); }
        else if (edge & KEY_RIGHT) { if (sel + 1u < head.count) select_to(sel + 1u); }
        else if (edge & KEY_UP) { if (sel >= COLS) select_to(sel - COLS); }
        else if (edge & KEY_DOWN) { if (sel + COLS < head.count) select_to(sel + COLS); else if (sel + 1u < head.count) select_to(head.count - 1u); }
        else if (edge & KEY_L1) { select_to(sel >= PER_PAGE ? sel - PER_PAGE : 0u); }
        else if (edge & KEY_R1) { select_to(sel + PER_PAGE < head.count ? sel + PER_PAGE : head.count - 1u); }
        else if (edge & KEY_A) open_from_grid();
        else if (edge & KEY_START) { run_pages(); draw_page(); prev = REG_RD(R_INPUT) & 0xFFFFu; }
    }
}
