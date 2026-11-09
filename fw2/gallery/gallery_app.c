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

#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(150, 150, 150)
#define ACC  RGB565(255, 170, 40)
#define BAD  RGB565(255, 80, 80)
#define COLS 4u
#define ROWS 4u
#define PER_PAGE (COLS * ROWS)
#define GX0 2u
#define GXS 100u
#define GY0 22u
#define GYS 76u
#define SLOT_PIC 2u
#define SLOT_IDX 5u
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
static int open_picture(uint32_t i, int show_progress)
{
    char path[GX_PATH_MAX + 2u]; path[0] = '/';
    { const char *s = ent[i - page * PER_PAGE].path; uint32_t n = 0; while (s[n] && n < GX_PATH_MAX) { path[1u + n] = s[n]; n++; } path[1u + n] = 0; }
    if (show_progress) say("OPENING", ACC);
    if (!slot_open_path(SLOT_PIC, path)) return 1;
    static uint8_t h4[4096]; gt_info_t t;
    if (!read_bytes(SLOT_PIC, 0u, 4096u, h4)) return 2;
    if (gt_parse(h4, 4096u, &t)) return 3;
    if (!(REG_RD(R_LAND_STATUS) >> 31)) return 4;                                   /* no landing path in this bitstream */
    const uint32_t total = (t.canvas_len + 4095u) & ~4095u;                         /* the file is padded to 4 KB: read the padding too, it lands just past the plane */
    REG_WR(R_LAND_BASE, GAL_PLANE_BASE); REG_WR(R_LAND_CTRL, 1u);
    for (uint32_t c = 0; c < total; c += LAND_CHUNK) {
        const uint32_t len = total - c < LAND_CHUNK ? total - c : LAND_CHUNK;
        if (!slot_read_blocking(SLOT_PIC, t.canvas_off + c, 0x40000000u + c, len, CLK_HZ * 4u)) return 5;
        if (show_progress) { char b[24], *q = put_s(b, "LOADING "); q = put_u(q, (c + len) * 100u / total); q = put_s(q, "%"); *q = 0; say(b, ACC); }
    }
    uint32_t t0 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
    if (REG_RD(R_LAND_OVF) != 0u || REG_RD(R_LAND_WORDS) != total / 4u) return 6;   /* a word was dropped or lost: do not show a damaged picture */
    return 0;
}
static const char *const open_msg[] = { "", "OPEN FAILED", "HEADER READ FAILED", "NOT A TGY PICTURE", "NO LANDING PATH", "PICTURE READ FAILED", "PICTURE DAMAGED IN TRANSFER" };

/* open_picture with the Info page's numbers: how long it took (ms), the landing's word counts (only when a landing ran), the status line text. */
static gi_t info = { 0u, 0u, 0u, GI_NONE, 0u, 0u, 0 };
static int open_timed(uint32_t i, int show_progress)
{
    const uint32_t t0 = cycles();
    const int r = open_picture(i, show_progress);
    info.open_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);
    if (r == 0 || r >= 5) { info.words = REG_RD(R_LAND_WORDS); info.lost = REG_RD(R_LAND_OVF); } else { info.words = 0u; info.lost = 0u; }
    info.err = r ? open_msg[r] : 0;
    return r;
}

static void viewer(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu;
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge & KEY_B) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); return; }
        if (edge & (KEY_LEFT | KEY_RIGHT)) {
            const uint32_t n = (edge & KEY_RIGHT) ? (sel + 1u) % head.count : (sel + head.count - 1u) % head.count;
            const uint32_t np = n / PER_PAGE;
            if (np != page) { page = np; if (!load_page()) { gal_view_off(); draw_page(); return; } }
            sel = n;
            const int r = open_timed(sel, 0);
            if (r) { gal_view_off(); loaded_page = 0xFFFFFFFFu; draw_page(); say(open_msg[r], BAD); return; }
        }
    }
}

static void open_from_grid(void)
{
    const int r = open_timed(sel, 1);
    if (r) { say(open_msg[r], BAD); return; }
    if (!gal_view_on()) { say("VIEWER DID NOT START", BAD); return; }
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
static void draw_info(void)
{
    gi_row_t r[GI_ROWS];
    info.core_version = REG_RD(R_VERSION); info.count = head.count; info.view_caps = REG_RD(R_VIEW_CAPS);
    gi_format(&info, r); page_clear("INFO");
    for (uint32_t i = 0; i < GI_ROWS; i++) { gfx_text(6, PAGE_Y0 + i * LINE_H, r[i].label, DIM, BG); gfx_text(150, PAGE_Y0 + i * LINE_H, r[i].value, i == 5u && info.err ? BAD : FG, BG); }
    page_foot("B BACK", 0, 0);
}
/* Waits for the next key press (an edge) and returns it. */
static uint32_t next_edge(uint32_t *prev)
{
    for (;;) { const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~*prev; *prev = keys; if (edge) return edge; }
}
static void run_pages(void)
{
    uint32_t prev = REG_RD(R_INPUT) & 0xFFFFu;
    gm_t m; gm_open(&m); draw_menu(&m);
    for (;;) {
        const uint32_t edge = next_edge(&prev); const int r = gm_input(&m, edge);
        if (r == GP_CLOSE) return;
        if (r == GP_STAY) { draw_menu(&m); continue; }
        if (r == GP_OPEN_CONTROLS) { gc_t c; gc_open(&c); draw_controls(&c); while (!gc_input(&c, next_edge(&prev))) draw_controls(&c); }
        else if (r == GP_OPEN_HELP) { gh_t h; gh_open(&h); draw_help(&h); while (!gh_input(&h, next_edge(&prev))) draw_help(&h); }
        else { draw_info(); while (!gi_input(next_edge(&prev))) draw_info(); }
        draw_menu(&m);
    }
}

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    platform_probe();
    gfx_text(6, 2, "TAU GALLERY", ACC, BG);
    { static uint8_t h[4096]; int rc = 100;
      if (read_bytes(SLOT_IDX, 0u, 4096u, h)) rc = gx_parse_head(h, GX_HEAD_BYTES, &head);
      if (rc == 100) { say("NO INDEX: TAU-LIBRARY.TDB NOT READ", BAD); for (;;) { } }
      if (rc) { char b[40], *q = put_s(b, "INDEX NOT USABLE E"); q = put_u(q, (uint32_t)rc); *q = 0; say(b, BAD); for (;;) { } }
      if (head.count == 0u) { say("NO PICTURES IN THE INDEX", BAD); for (;;) { } } }
    draw_page();
    uint32_t prev = 0;
    for (;;) {
        const uint32_t keys = REG_RD(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
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
