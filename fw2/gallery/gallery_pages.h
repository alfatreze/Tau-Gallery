/* fw2/gallery/gallery_pages.h -- Tau Gallery M1.6 slice 1 (docs/M1_DESIGN.md section 5): the menu, the Controls list and the Help pages. Pure: no drawing, no registers. Host-tested (sim/test_gallery_pages.py).
 * Start on the grid opens the menu; Up/Down move, A opens an entry, B or Start closes. Controls is a read-only list (Up/Down scroll, wrapping, Helios's rule); Help is three pages of text (Up/Down turn the
 * page, wrapping). B or Left leaves a page for the menu. Lines are at most GP_MAXCH characters so they fit the 400 px screen. Info (slice 2) shows the core, the picture count and the last open; Settings joins the menu in a later slice. */
#ifndef GALLERY_PAGES_H
#define GALLERY_PAGES_H
#include <stdint.h>
#include "../cores/helios_core.h"
#include "../cores/theme_core.h"
#ifndef BUG
#define BUG 0
#endif
#define GP_MAXCH 36u
#define GP_KEY_UP    (1u << 0)
#define GP_KEY_DOWN  (1u << 1)
#define GP_KEY_LEFT  (1u << 2)
#define GP_KEY_RIGHT (1u << 3)
#define GP_KEY_A     (1u << 4)
#define GP_KEY_B     (1u << 5)
#define GP_KEY_SELECT (1u << 14)
#define GP_KEY_START (1u << 15)

/* ---- the menu ---- */
enum { GM_CONTROLS = 0, GM_HELP, GM_INFO, GM_SETTINGS, GM_THEME, GM_COUNT };
static const char *const gm_names[GM_COUNT] = { "CONTROLS", "HELP", "INFO", "SETTINGS", "THEME" };
enum { GP_STAY = 0, GP_CLOSE, GP_OPEN_CONTROLS, GP_OPEN_HELP, GP_OPEN_INFO, GP_OPEN_SETTINGS, GP_OPEN_THEME };      /* what a key edge asks of the caller */
typedef struct { uint8_t sel; } gm_t;
static inline void gm_open(gm_t *m) { m->sel = 0; }
static inline int gm_input(gm_t *m, uint32_t edge)
{
    if (edge & (GP_KEY_UP | GP_KEY_DOWN)) {
#if BUG == 1
        m->sel = (uint8_t)((edge & GP_KEY_UP) ? (m->sel ? m->sel - 1u : 0u) : (m->sel + 1u < GM_COUNT ? m->sel + 1u : GM_COUNT - 1u));      /* clamps instead of wrapping */
#else
        m->sel = (uint8_t)hl_wrap(m->sel, GM_COUNT, (edge & GP_KEY_UP) != 0);
#endif
        return GP_STAY;
    }
    if (edge & GP_KEY_A) {
#if BUG == 2
        return m->sel == GM_CONTROLS ? GP_OPEN_HELP : m->sel == GM_HELP ? GP_OPEN_CONTROLS : m->sel == GM_INFO ? GP_OPEN_INFO : m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_THEME;      /* the first two entries swapped */
#else
        return m->sel == GM_CONTROLS ? GP_OPEN_CONTROLS : m->sel == GM_HELP ? GP_OPEN_HELP : m->sel == GM_INFO ? GP_OPEN_INFO : m->sel == GM_SETTINGS ? GP_OPEN_SETTINGS : GP_OPEN_THEME;
#endif
    }
    return (edge & (GP_KEY_B | GP_KEY_START)) ? GP_CLOSE : GP_STAY;
}

/* ---- Controls: every button the gallery uses, as the code reads them (gallery_app.c main loop and viewer) ---- */
typedef struct { const char *key, *what; } gc_row_t;
static const gc_row_t gc_rows[] = {
    { "GRID",           ""                    },
    { "LEFT / RIGHT",   "PREVIOUS / NEXT"     },
    { "UP / DOWN",      "ONE ROW UP / DOWN"   },
    { "L1 / R1",        "PAGE BACK / ON"      },
    { "A",              "OPEN THE PICTURE"    },
    { "START",          "MENU"                },
    { "SELECT",         "PICTURE DETAILS"     },
    { "PICTURE",        ""                    },
    { "LEFT / RIGHT",   "PREVIOUS / NEXT"     },
    { "A",              "ZOOM IN (IF IT CAN)" },
    { "B",              "BACK TO THE GRID"    },
    { "SELECT",         "PICTURE DETAILS"     },
    { "X",              "NEXT COLOUR FILTER"  },
    { "Y",              "TURN RIGHT"          },
    { "START + X / Y",  "MIRROR SIDE / TOP"   },
    { "L1 / START",     "CROP MASK / TURN IT" },
    { "ZOOMED IN",      ""                    },
    { "D-PAD",          "PAN"                 },
    { "START",          "MAP: JUMP TO A SPOT" },
    { "L1 / R1",        "ZOOM OUT / IN"       },
    { "R1 AT 1:1",      "2X: EVERY PIXEL DOUBLED" },
    { "L1 / B AT 2X",   "BACK TO 1:1"         },
    { "A",              "BACK TO FIT"         },
    { "B",              "BACK TO FIT"         },
    { "SELECT + L / R", "PREVIOUS / NEXT"     },
    { "MENU AND PAGES", ""                    },
    { "UP / DOWN",      "MOVE / SCROLL / PAGE"},
    { "A",              "OPEN"                },
    { "B / LEFT",       "BACK"                },
    { "START",          "CLOSE THE MENU"      },
};
#define GC_N ((uint8_t)(sizeof(gc_rows) / sizeof(gc_rows[0])))
#define GC_VIS 12u                                                   /* rows on the screen at once */
typedef struct { uint8_t sel, top; } gc_t;
static inline void gc_open(gc_t *c) { c->sel = 0; c->top = 0; }
static inline void gc_fix(gc_t *c)
{
    uint32_t sel = c->sel, top = c->top;
    if (sel < top) top = sel;
    if (sel >= top + GC_VIS) top = sel + 1u - GC_VIS;
    c->top = (uint8_t)top;
}
/* One key edge: returns 1 when the page should close (back to the menu). */
static inline int gc_input(gc_t *c, uint32_t edge)
{
    if (edge & (GP_KEY_UP | GP_KEY_DOWN)) {
#if BUG == 3
        c->sel = (uint8_t)((edge & GP_KEY_UP) ? (c->sel ? c->sel - 1u : 0u) : (c->sel + 1u < GC_N ? c->sel + 1u : GC_N - 1u));      /* clamps instead of wrapping */
#else
        c->sel = (uint8_t)hl_wrap(c->sel, GC_N, (edge & GP_KEY_UP) != 0);
#endif
        gc_fix(c); return 0;
    }
    return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0;
}

/* ---- Help: three pages of text ---- */
enum { GS_TEXT = 0, GS_ACCENT, GS_DIM, GS_GAP };                      /* a line's style; GS_GAP is an empty line */
typedef struct { uint8_t style; const char *s; } gh_line_t;
typedef struct { const char *title; const gh_line_t *lines; uint8_t n; } gh_page_t;
static const gh_line_t gh_p1[] = {
    { GS_TEXT, "Convert pictures on a computer:" }, { GS_GAP, "" },
    { GS_ACCENT, "python3 tools/gallery/tgy_convert.py" }, { GS_TEXT, "  PHOTOS_DIR OUT_DIR" }, { GS_GAP, "" },
    { GS_TEXT, "It writes one .tgy file for each" }, { GS_TEXT, "picture and the index" }, { GS_ACCENT, "tau-library.tdb" }, { GS_GAP, "" },
    { GS_DIM, "Or: package_dev_build.py --variant" }, { GS_DIM, "galleryapp --photos DIR" } };
static const gh_line_t gh_p2[] = {
    { GS_TEXT, "On the card:" }, { GS_GAP, "" },
    { GS_ACCENT, "Assets/tau_gallery/common/photos/" }, { GS_TEXT, "  the .tgy pictures" },
    { GS_ACCENT, "Assets/tau_gallery/common/" }, { GS_TEXT, "  tau-library.tdb, the index" },
    { GS_ACCENT, "Memories/Screenshots/" }, { GS_TEXT, "  Menu + Start screenshots" }, { GS_GAP, "" },
    { GS_DIM, "The index lists every picture and" }, { GS_DIM, "its thumbnail; convert to change it." } };
static const gh_line_t gh_p3[] = {
    { GS_TEXT, "A red line at the bottom says why a" }, { GS_TEXT, "picture did not open:" }, { GS_GAP, "" },
    { GS_ACCENT, "OPEN FAILED" }, { GS_TEXT, "  the .tgy file is not on the card" },
    { GS_ACCENT, "NOT A TGY PICTURE" }, { GS_TEXT, "  the file is damaged or other" },
    { GS_ACCENT, "PICTURE READ FAILED" }, { GS_TEXT, "  the card did not answer" },
    { GS_ACCENT, "DAMAGED IN TRANSFER" }, { GS_TEXT, "  a word was lost: try again" } };
#define GH_N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
static const gh_page_t gh_pages[] = { { "ADD PHOTOS", gh_p1, GH_N(gh_p1) }, { "WHERE THINGS ARE", gh_p2, GH_N(gh_p2) }, { "IF A PICTURE FAILS", gh_p3, GH_N(gh_p3) } };
#define GH_PAGES ((uint8_t)(sizeof(gh_pages) / sizeof(gh_pages[0])))
typedef struct { uint8_t page; } gh_t;
static inline void gh_open(gh_t *h) { h->page = 0; }
/* One key edge: returns 1 when Help should close (back to the menu). */
static inline int gh_input(gh_t *h, uint32_t edge)
{
    if (edge & (GP_KEY_UP | GP_KEY_DOWN)) {
#if BUG == 4
        h->page = (uint8_t)((edge & GP_KEY_UP) ? (h->page ? h->page - 1u : 0u) : (h->page + 1u < GH_PAGES ? h->page + 1u : GH_PAGES - 1u));      /* clamps instead of wrapping */
#else
        h->page = (uint8_t)hl_wrap(h->page, GH_PAGES, (edge & GP_KEY_UP) != 0);
#endif
        return 0;
    }
    return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0;
}

/* ---- Settings: rows RESUME (the grid opens on the last picture), GRID SIZE (4 x 3, 5 x 4, 6 x 4, 8 x 6) and DITHER (off, ordered, ordered + moving). Up and Down choose the row, Left, Right and A change its value (wrapping); B goes back. ---- */
enum { GSP_RESUME = 0, GSP_GRID, GSP_DITHER, GSP_COLOUR, GSP_ROWS };
#define GSP_LUTS 8u
#define GSP_GRIDS 4u
#define GSP_DITHERS 3u
typedef struct { uint8_t row; } gsp_t;
static const char *const gsp_labels[GSP_ROWS] = { "RESUME LAST PICTURE", "GRID SIZE", "DITHER", "COLOUR" };
static const char *const gsp_grid_names[GSP_GRIDS] = { "4 X 3", "5 X 4", "6 X 4", "8 X 6" };
static const char *const gsp_dither_names[GSP_DITHERS] = { "OFF", "ORDERED", "MOVING" };
/* One key edge: `resume` (0 or 1), `grid` (0..3) and `dither` (0..2) and `lut` (0..7, the colour preset) are the live values; returns 1 when the page should close. */
static inline int gsp_input(gsp_t *s, uint32_t *resume, uint32_t *grid, uint32_t *dither, uint32_t *lut, uint32_t edge)
{
    if (edge & (GP_KEY_UP | GP_KEY_DOWN)) { s->row = (uint8_t)hl_wrap(s->row, GSP_ROWS, (edge & GP_KEY_UP) != 0); return 0; }
    if (edge & (GP_KEY_LEFT | GP_KEY_A | (1u << 3))) {                                           /* bit 3 is Right */
        const int dec = (edge & GP_KEY_LEFT) != 0;
        if (s->row == GSP_RESUME) {
#if BUG == 7
            (void)resume;                                                                        /* nothing changes */
#else
            *resume = (*resume & 1u) ^ 1u;
#endif
        }
        else if (s->row == GSP_GRID) *grid = hl_wrap(*grid % GSP_GRIDS, GSP_GRIDS, dec);
        else if (s->row == GSP_DITHER) *dither = hl_wrap(*dither % GSP_DITHERS, GSP_DITHERS, dec);
        else *lut = hl_wrap(*lut % GSP_LUTS, GSP_LUTS, dec);
        return 0;
    }
    return (edge & GP_KEY_B) != 0;
}

/* ---- Theme: three rows, THEME, MODE (dark or light) and ACCENT (the 19 Pocket colours). Up/Down choose the row, Left/Right change its value (wrapping), B goes back. ---- */
enum { GT_THEME = 0, GT_MODE, GT_ACCENT, GT_ROWS };
static const char *const gt_labels[GT_ROWS] = { "THEME", "MODE", "ACCENT" };
enum { GT_STAY = 0, GT_CHANGED, GT_CLOSE };
typedef struct { uint8_t sel; } gt_t;
typedef struct { uint32_t theme, pol, pal; } gt_look_t;
static inline void gt_open(gt_t *g) { g->sel = 0; }
/* One key edge: `l` is the live look, `theme_n` how many themes this build has. */
static inline int gt_input(gt_t *g, gt_look_t *l, uint32_t theme_n, uint32_t edge)
{
    if (edge & (GP_KEY_UP | GP_KEY_DOWN)) { g->sel = (uint8_t)hl_wrap(g->sel, GT_ROWS, (edge & GP_KEY_UP) != 0); return GT_STAY; }
    if (edge & (GP_KEY_LEFT | (1u << 3))) {                                                      /* bit 3 is Right */
        const int dec = (edge & GP_KEY_LEFT) != 0;
#if BUG == 8
        if (g->sel == GT_THEME) l->theme = dec ? (l->theme ? l->theme - 1u : theme_n - 1u) : (l->theme + 1u < theme_n ? l->theme + 1u : theme_n - 1u);      /* Right clamps */
#else
        if (g->sel == GT_THEME) l->theme = hl_wrap(l->theme, theme_n, dec);
#endif
        else if (g->sel == GT_MODE) l->pol = hl_wrap(l->pol, 2u, dec);
        else l->pal = hl_wrap(l->pal, TC_PALETTE_N, dec);
        return GT_CHANGED;
    }
    return (edge & GP_KEY_B) ? GT_CLOSE : GT_STAY;
}

/* ---- Info: what the firmware knows about itself and the last picture it opened (M1.6 slice 2) ---- */
#define GI_NONE 0xFFFFFFFFu                                          /* open_ms before any picture was opened */
#define GI_ROWS 19u
typedef struct { uint32_t core_version, count, view_caps, open_ms, words, lost; const char *err; uint32_t set_layout, set_loads, set_stores, zoom_ms, land_ms, copy_ms, flip_ms;
    uint32_t zt_n, zt_land_sum, zt_land_max, zt_dec_n, zt_dec_sum, zt_dec_max, zt_pass_max, zt_catch, zt_moved, zt_stall_run_max, pipe_off, zt_sp, zt_sc, zt_sn, zt_sj, zt_ss; } gi_t;      /* zt_s*: where the longest pass spent its microseconds: planning, Talos copy step, needed list, the tile, the rest */      /* the version 1 zoom's tile statistics of the last zoom (microseconds; zt_catch frames the shown view was behind the wanted corner, zt_moved of them it moved) */      /* view_caps: R_VIEW_CAPS (bit 1 = the 800 x 720 viewer); err: the last status line, 0 = none */
typedef struct { const char *label; char value[28]; } gi_row_t;
static inline char *gi_s(char *q, const char *p) { while (*p) *q++ = *p++; return q; }
static inline char *gi_u(char *q, uint32_t v) { char t[10]; uint32_t n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *q++ = t[--n]; return q; }
static inline uint32_t gi_cap(uint32_t v, uint32_t m) { return v > m ? m : v; }
static inline char *gi_hex8(char *q, uint32_t v) { for (int i = 28; i >= 0; i -= 4) *q++ = "0123456789ABCDEF"[(v >> i) & 15u]; return q; }
static inline char *gi_ms0(char *q, uint32_t us) { uint32_t ms = (us + 500u) / 1000u; return gi_u(q, ms > 99u ? 99u : ms); }      /* whole milliseconds, rounded, capped at 99 */
static inline char *gi_us(char *q, uint32_t us)          /* microseconds as milliseconds with one decimal, capped at 999.9 */
{
    if (us > 999900u) us = 999900u;
    q = gi_u(q, us / 1000u); *q++ = '.'; *q++ = (char)('0' + (us % 1000u) / 100u); return q;
}
static inline void gi_format(const gi_t *d, gi_row_t r[GI_ROWS])
{
    char *q;
    r[0].label = "CORE VERSION"; q = gi_hex8(r[0].value, d->core_version); *q = 0;
    r[1].label = "PICTURES";     q = gi_u(r[1].value, d->count); *q = 0;
#if BUG == 5
    r[2].label = "VIEWER";       q = gi_s(r[2].value, (d->view_caps & 1u) ? "800 X 720" : "NO VIEWER"); *q = 0;       /* the mode-sequencer bit instead of the viewer bit */
#else
    r[2].label = "VIEWER";       q = gi_s(r[2].value, (d->view_caps & 2u) ? "800 X 720" : "NO VIEWER"); *q = 0;
#endif
    r[3].label = "LAST OPEN";
    if (d->open_ms == GI_NONE) { q = gi_s(r[3].value, "NONE YET"); *q = 0; }
    else {
#if BUG == 6
        q = gi_u(r[3].value, d->open_ms / 1000u); q = gi_s(q, " MS"); *q = 0;                                    /* seconds shown as milliseconds */
#else
        q = gi_u(r[3].value, d->open_ms); q = gi_s(q, " MS"); *q = 0;
#endif
    }
    r[4].label = "LAST LOAD";
    if (d->open_ms == GI_NONE) { q = gi_s(r[4].value, "NONE YET"); *q = 0; }
    else { q = gi_u(r[4].value, d->words); q = gi_s(q, " WORDS, "); q = gi_u(q, d->lost); q = gi_s(q, " LOST"); *q = 0; }
    r[5].label = "LAST ERROR";   q = gi_s(r[5].value, d->err ? d->err : "NONE"); *q = 0;
    r[6].label = "SETTINGS";     q = gi_s(r[6].value, "L"); q = gi_u(q, gi_cap(d->set_layout, 15u)); q = gi_s(q, " LOADED "); q = gi_u(q, gi_cap(d->set_loads, 9999u)); q = gi_s(q, " SAVED "); q = gi_u(q, gi_cap(d->set_stores, 9999u)); *q = 0;      /* the layout version in the file, how often it was read and written: the proof of a round trip */
    r[7].label = "ZOOM OPEN";    if (d->zoom_ms == GI_NONE) q = gi_s(r[7].value, "NONE YET"); else { q = gi_u(r[7].value, d->zoom_ms); q = gi_s(q, " MS"); } *q = 0;       /* from the key to the first zoomed view on screen */
#if BUG == 9
    r[8].label = "ZOOM LAND";    if (d->zoom_ms == GI_NONE) q = gi_s(r[8].value, "NONE YET"); else { q = gi_u(r[8].value, d->zoom_ms); q = gi_s(q, " MS"); } *q = 0;                /* shows the first-view time in the land row */
#else
    r[8].label = "ZOOM LAND";    if (d->land_ms == GI_NONE) q = gi_s(r[8].value, "NONE YET"); else { q = gi_u(r[8].value, d->land_ms); q = gi_s(q, " MS"); } *q = 0;       /* the last tile or chunk of 48 KB, card to SDRAM */
#endif
    r[9].label = "ZOOM COPY";    if (d->copy_ms == GI_NONE) q = gi_s(r[9].value, "NONE YET"); else { q = gi_u(r[9].value, d->copy_ms); q = gi_s(q, " MS"); } *q = 0;       /* the last tile through Talos, its 256 commands and the fence */
    r[10].label = "ZOOM FLIP";   if (d->flip_ms == GI_NONE) q = gi_s(r[10].value, "NONE YET"); else { q = gi_u(r[10].value, d->flip_ms); q = gi_s(q, " MS"); } *q = 0;       /* the last window re-centre, from its start to the flip */
    r[11].label = "TILES READ";
    if (d->zt_n == 0u) { q = gi_s(r[11].value, "NONE YET"); *q = 0; }
    else {
        q = gi_u(r[11].value, gi_cap(d->zt_n, 9999u)); q = gi_s(q, " AVG ");
#if BUG == 14
        q = gi_us(q, d->zt_land_max); q = gi_s(q, " MAX "); q = gi_us(q, d->zt_land_max); q = gi_s(q, " MS"); *q = 0;                    /* the maximum twice */
#else
        q = gi_us(q, d->zt_land_sum / d->zt_n); q = gi_s(q, " MAX "); q = gi_us(q, d->zt_land_max); q = gi_s(q, " MS"); *q = 0;
#endif
    }
    r[12].label = "TILES DECODED";
    if (d->zt_dec_n == 0u) { q = gi_s(r[12].value, "NONE YET"); *q = 0; }
    else { q = gi_u(r[12].value, gi_cap(d->zt_dec_n, 9999u)); q = gi_s(q, " DONE, "); q = gi_u(q, gi_cap(d->zt_n > d->zt_dec_n ? d->zt_n - d->zt_dec_n : 0u, 9999u)); q = gi_s(q, " UNUSED"); *q = 0; }
    r[13].label = "TILE DECODE";
    if (d->zt_dec_n == 0u) { q = gi_s(r[13].value, "NONE YET"); *q = 0; }
    else { q = gi_s(r[13].value, "AVG "); q = gi_us(q, d->zt_dec_sum / d->zt_dec_n); q = gi_s(q, " MAX "); q = gi_us(q, d->zt_dec_max); q = gi_s(q, " MS"); *q = 0; }
    r[14].label = "LONGEST PASS";
    if (d->zt_pass_max == 0u) { q = gi_s(r[14].value, "NONE YET"); *q = 0; } else { q = gi_us(r[14].value, d->zt_pass_max); q = gi_s(q, " MS"); *q = 0; }
    r[15].label = "PAN STALLS";
    if (d->zt_catch == 0u) { q = gi_s(r[15].value, "NONE YET"); *q = 0; }
    else {
#if BUG == 15
        q = gi_u(r[15].value, gi_cap(d->zt_moved, 99999u));                                                                              /* the frames that moved, not the stalls */
#else
        q = gi_u(r[15].value, gi_cap(d->zt_catch > d->zt_moved ? d->zt_catch - d->zt_moved : 0u, 99999u));
#endif
        q = gi_s(q, " OF "); q = gi_u(q, gi_cap(d->zt_catch, 99999u)); q = gi_s(q, " FRAMES"); *q = 0;
    }
    r[16].label = "LONGEST STALL";
    if (d->zt_catch == 0u) { q = gi_s(r[16].value, "NONE YET"); *q = 0; }
    else { q = gi_u(r[16].value, gi_cap(d->zt_stall_run_max, 99999u)); q = gi_s(q, d->zt_stall_run_max == 1u ? " FRAME IN A ROW" : " FRAMES IN A ROW"); *q = 0; }
    r[18].label = "PASS SPLIT";
    if (d->zt_pass_max == 0u) { q = gi_s(r[18].value, "NONE YET"); *q = 0; }
    else {
        q = gi_s(r[18].value, "P"); q = gi_ms0(q, d->zt_sp); q = gi_s(q, " C"); q = gi_ms0(q, d->zt_sc); q = gi_s(q, " N"); q = gi_ms0(q, d->zt_sn);
        q = gi_s(q, " J"); q = gi_ms0(q, d->zt_sj); q = gi_s(q, " S"); q = gi_ms0(q, d->zt_ss); q = gi_s(q, " MS"); *q = 0;               /* plan, Talos copy, needed list, job (the tile's card read and decode), show */
    }
    r[17].label = "ZOOM PIPELINE"; q = gi_s(r[17].value, d->pipe_off ? "OFF" : "ON"); *q = 0;          /* the next tile's card read overlapped with a decode; A on this page switches it */
}
/* One key edge: 1 when the page should close (back to the menu), 2 for A (switch the zoom pipeline), else 0. */
static inline int gi_input(uint32_t edge) { return (edge & (GP_KEY_LEFT | GP_KEY_B)) ? 1 : (edge & GP_KEY_A) ? 2 : 0; }      /* 1 closes (B or Left), 2 is A: the caller switches the zoom pipeline */

/* ---- Details: one picture's information and its histograms (a PLACEHOLDER page: the rows that need data the firmware does not read yet say NOT READ YET, and the four histogram frames are empty).
 * Select opens it from the grid (the selected picture) and from the picture view (not while zoomed in: Select + Left / Right is the zoomed view's previous / next); B or Select closes it. ---- */
#define GD_ROWS 8u
#define GD_HIST_N 4u
#define GD_NOT_READ "NOT READ YET"
#define GD_HIST_NOTE "NOT COMPUTED YET"                                            /* a frame without a thumbnail to count (its read failed) */
#define GD_HIST_SRC "FROM THE THUMBNAIL"
#define GD_HIST_SRC_CROP "FROM THE THUMBNAIL'S CROP"                                  /* a version 1 index's thumbnail is the picture cropped to a square: only its centre is counted */
#define GD_HIST_WAIT "COUNTING THE PICTURE ..."
#define GD_HIST_SRC_DEC "COUNTED FROM THE DECODED PICTURE"
/* What the viewer's keys do, shown under the preview on the INFO and SHOT pages (Select opens them from the grid or the picture), 8 pixels a character from x 112. */
#define GD_KEYS_N 6u
static const char *const gd_keys[GD_KEYS_N] = { "IN THE PICTURE", "X  NEXT COLOUR FILTER", "Y  TURN RIGHT", "START + X / Y  MIRROR", "L1  CROP MASK", "A  ZOOM   B  BACK" };
static const char *const gd_hist_labels[GD_HIST_N] = { "LUMA", "RED", "GREEN", "BLUE" };
typedef struct { const char *name; uint32_t index, count, size, width, height; } gd_t;          /* index is 0-based; size is the file's bytes; width and height 0 when the index does not say */
typedef struct { const char *label; char value[28]; } gd_row_t;
/* "12.3 MB" from 1 MiB up (one decimal, rounded down), "812 KB" from 1 KiB, "512 B" below */
static inline char *gd_size(char *q, uint32_t bytes)
{
    if (bytes >= 1048576u) { const uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10u) / 1048576u); q = gi_u(q, tenths / 10u); *q++ = '.'; q = gi_u(q, tenths % 10u); return gi_s(q, " MB"); }
    if (bytes >= 1024u) { q = gi_u(q, bytes / 1024u); return gi_s(q, " KB"); }
    q = gi_u(q, bytes); return gi_s(q, " B");
}
static inline void gd_format(const gd_t *d, gd_row_t r[GD_ROWS])
{
    char *q; uint32_t n;
    r[0].label = "NAME"; q = r[0].value; for (n = 0; d->name[n] && n < 26u; n++) *q++ = d->name[n]; *q = 0;                          /* cut to the column, never past it */
    r[1].label = "PICTURE";
#if BUG == 10
    q = gi_u(r[1].value, d->index); q = gi_s(q, " OF "); q = gi_u(q, d->count); *q = 0;                                               /* 0-based */
#else
    q = gi_u(r[1].value, d->index + 1u); q = gi_s(q, " OF "); q = gi_u(q, d->count); *q = 0;
#endif
    r[2].label = "SIZE";
    if (d->width == 0u || d->height == 0u) { q = gi_s(r[2].value, "UNKNOWN"); *q = 0; }
#if BUG == 11
    else { q = gi_u(r[2].value, d->height); q = gi_s(q, " X "); q = gi_u(q, d->width); *q = 0; }                                       /* height first */
#else
    else { q = gi_u(r[2].value, d->width); q = gi_s(q, " X "); q = gi_u(q, d->height); *q = 0; }
#endif
    r[3].label = "FILE";
#if BUG == 12
    q = gi_u(r[3].value, d->size / 1024u); q = gi_s(q, " MB"); *q = 0;                                                               /* KB with the MB unit */
#else
    q = gd_size(r[3].value, d->size); *q = 0;
#endif
}
/* ---- Gallery 1.0 details: the items the index has for a picture (docs/features/TGIX_V1_SPEC.md 2.4: `tag, length, bytes`, only the fields the picture has) as text, in pages. Nothing is drawn for a field the picture lacks. ---- */
#define GDM_FORMAT 1u
#define GDM_CAMERA 2u
#define GDM_LENS 4u
#define GDM_TAKEN 8u
#define GDM_SHUTTER 16u
#define GDM_APERTURE 32u
#define GDM_ISO 64u
#define GDM_FOCAL 128u
#define GDM_SHOT (GDM_SHUTTER | GDM_APERTURE | GDM_ISO | GDM_FOCAL)
typedef struct { uint32_t has; char format[16], camera[41], lens[41]; uint16_t year; uint8_t mon, day, hour, min, sec; uint32_t shutter_n, shutter_d, iso; uint16_t aperture10, focal10; } gd_meta_t;
static inline uint32_t gd_le(const uint8_t *p, uint32_t n) { uint32_t v = 0; for (uint32_t i = 0; i < n; i++) v |= (uint32_t)p[i] << (8u * i); return v; }
static inline void gd_cp(char *d, const uint8_t *s, uint32_t n, uint32_t max) { uint32_t i = 0; while (i < n && i < max && s[i]) { d[i] = (char)s[i]; i++; } d[i] = 0; }
/* The items of a details run. An unknown tag is skipped, an item of the wrong size is ignored, an item that reaches past the run ends the parse (what came before it counts). */
static inline void gd_meta_parse(const uint8_t *d, uint32_t n, gd_meta_t *m)
{
    m->has = 0u; m->format[0] = m->camera[0] = m->lens[0] = 0;
    uint32_t i = 0;
    while (i + 2u <= n) {
        const uint32_t tag = d[i], len = d[i + 1u];
        if (i + 2u + len > n) break;
        const uint8_t *v = d + i + 2u;
        if (tag == 1u) { gd_cp(m->format, v, len, 15u); if (m->format[0]) m->has |= GDM_FORMAT; }
        else if (tag == 2u) { gd_cp(m->camera, v, len, 40u); if (m->camera[0]) m->has |= GDM_CAMERA; }
        else if (tag == 3u) { gd_cp(m->lens, v, len, 40u); if (m->lens[0]) m->has |= GDM_LENS; }
        else if (tag == 4u && len == 7u) { m->year = (uint16_t)gd_le(v, 2u); m->mon = v[2]; m->day = v[3]; m->hour = v[4]; m->min = v[5]; m->sec = v[6]; if (m->year >= 1990u && m->mon >= 1u && m->mon <= 12u && m->day >= 1u && m->day <= 31u && m->hour < 24u && m->min < 60u) m->has |= GDM_TAKEN; }
        else if (tag == 5u && len == 8u) { m->shutter_n = gd_le(v, 4u); m->shutter_d = gd_le(v + 4, 4u); if (m->shutter_n && m->shutter_d) m->has |= GDM_SHUTTER; }
        else if (tag == 6u && len == 2u) { m->aperture10 = (uint16_t)gd_le(v, 2u); if (m->aperture10) m->has |= GDM_APERTURE; }
        else if (tag == 7u && len == 4u) { m->iso = gd_le(v, 4u); if (m->iso) m->has |= GDM_ISO; }
        else if (tag == 8u && len == 2u) { m->focal10 = (uint16_t)gd_le(v, 2u); if (m->focal10) m->has |= GDM_FOCAL; }
        i += 2u + len;
    }
}
static inline char *gd_u2(char *q, uint32_t v) { *q++ = (char)('0' + (v / 10u) % 10u); *q++ = (char)('0' + v % 10u); return q; }
/* The INFO page: NAME, PICTURE, SIZE, FILE always; FORMAT, CAMERA, LENS, TAKEN only when the picture has them. Returns the number of rows. */
static inline uint32_t gdi_rows(const gd_t *d, const gd_meta_t *m, gd_row_t r[GD_ROWS])
{
    gd_format(d, r); uint32_t n = 4u; char *q;
    if (m->has & GDM_FORMAT) { r[n].label = "FORMAT"; q = gi_s(r[n].value, m->format); *q = 0; n++; }
    if (m->has & GDM_CAMERA) { r[n].label = "CAMERA"; q = r[n].value; for (uint32_t i = 0; m->camera[i] && i < 26u; i++) *q++ = m->camera[i]; *q = 0; n++; }
    if (m->has & GDM_LENS) { r[n].label = "LENS"; q = r[n].value; for (uint32_t i = 0; m->lens[i] && i < 26u; i++) *q++ = m->lens[i]; *q = 0; n++; }
    if (m->has & GDM_TAKEN) {
        r[n].label = "TAKEN"; q = gi_u(r[n].value, m->year); *q++ = '-'; q = gd_u2(q, m->mon); *q++ = '-'; q = gd_u2(q, m->day); *q++ = ' '; q = gd_u2(q, m->hour); *q++ = ':'; q = gd_u2(q, m->min); *q = 0; n++;
    }
    return n;
}
/* The SHOT page: SHUTTER, APERTURE, ISO, FOCAL LENGTH, the ones the picture has. 1/250 S, 2 S, 2.5 S; F/2.8; 400; 35 MM or 17.5 MM. */
static inline uint32_t gds_rows(const gd_meta_t *m, gd_row_t r[GD_ROWS])
{
    uint32_t n = 0; char *q;
    if (m->has & GDM_SHUTTER) {
        r[n].label = "SHUTTER"; q = r[n].value;
        if (m->shutter_n == 1u) { q = gi_s(q, "1/"); q = gi_u(q, m->shutter_d); }
        else if (m->shutter_d == 1u) q = gi_u(q, m->shutter_n);
        else { const uint32_t t = (uint32_t)(((uint64_t)m->shutter_n * 10u + m->shutter_d / 2u) / m->shutter_d); q = gi_u(q, t / 10u); *q++ = '.'; q = gi_u(q, t % 10u); }
        q = gi_s(q, " S"); *q = 0; n++;
    }
    if (m->has & GDM_APERTURE) { r[n].label = "APERTURE"; q = gi_s(r[n].value, "F/"); q = gi_u(q, m->aperture10 / 10u); *q++ = '.'; q = gi_u(q, m->aperture10 % 10u); *q = 0; n++; }
    if (m->has & GDM_ISO) { r[n].label = "ISO"; q = gi_u(r[n].value, m->iso); *q = 0; n++; }
    if (m->has & GDM_FOCAL) { r[n].label = "FOCAL LENGTH"; q = gi_u(r[n].value, m->focal10 / 10u); if (m->focal10 % 10u) { *q++ = '.'; q = gi_u(q, m->focal10 % 10u); } q = gi_s(q, " MM"); *q = 0; n++; }
    return n;
}
/* The pages: INFO, SHOT (only when the picture has an exposure field) and HISTOGRAMS; Left and Right turn them (wrapping), B or Select closes. The indicator is "page/pages" with one dot a page. */
enum { GDP_INFO = 0, GDP_SHOT, GDP_HIST };
typedef struct { uint8_t n, cur, id[3]; } gdp_t;
static inline void gdp_open(gdp_t *p, const gd_meta_t *m)
{
    p->n = 0u; p->id[p->n++] = GDP_INFO;
    if (m->has & GDM_SHOT) p->id[p->n++] = GDP_SHOT;
    p->id[p->n++] = GDP_HIST; p->cur = 0u;
}
static inline int gdp_input(gdp_t *p, uint32_t edge)
{
    if (edge & (GP_KEY_LEFT | GP_KEY_RIGHT)) { p->cur = (uint8_t)hl_wrap(p->cur, p->n, (edge & GP_KEY_LEFT) != 0); return 0; }
    return (edge & (GP_KEY_B | GP_KEY_SELECT)) != 0;
}
static inline char *gdp_indicator(char *q, const gdp_t *p) { q = gi_u(q, (uint32_t)p->cur + 1u); *q++ = '/'; q = gi_u(q, p->n); *q = 0; return q; }
/* The histograms: 32 bins each of luma, red, green and blue, counted from the 96 x 72 thumbnail in RGB565 (the CPU does not read the picture plane). Red and blue use their 5 bits as they are, green its top 5 of 6, luma
 * (77 R + 150 G + 29 B) / 256 on the 8-bit expanded channels, then / 8. A bar is as tall as its count is to the tallest bin of the same histogram (at least 1 pixel for a bin that has any). */
#define HS_BINS 32u
#define HS_FINE 256u                                                                              /* the decoder's own resolution: one bin for each of the 256 levels */
/* bin: 32 levels counted from the thumbnail (RGB565 has no more steps than that to tell apart); fine: all 256 levels counted by the decoder's histogram unit, is_fine says which of the two the page draws. */
typedef struct { uint16_t bin[GD_HIST_N][HS_BINS]; uint16_t fine[GD_HIST_N][HS_FINE]; uint32_t n; uint8_t is_fine; } hs_t;
/* The counts of the decoder (all 256 levels of each histogram, up to 24 bits each) into the 16 bit fine bins of an hs_t: every bin is shifted right by the least s that brings the largest under 65,536, and a bin that had any pixels keeps at least 1 (the bars only compare bins of one histogram). n is the real count. */
static inline uint32_t hs_shift(uint32_t mx)                                                          /* the least shift that brings the largest count under 65,536 */
{
    uint32_t s = 0u;
#if BUG == 21
    (void)mx;                                                                                        /* no scaling: a count above 65,535 wraps */
#else
    while ((mx >> s) > 65535u) s++;
#endif
    return s;
}
static inline uint16_t hs_scale(uint32_t c, uint32_t s) { const uint32_t v = c >> s; return (uint16_t)((v == 0u && c != 0u) ? 1u : v); }      /* a bin that had pixels keeps at least 1 */
static inline void hs_from_counts(hs_t *h, const uint32_t c[GD_HIST_N][HS_FINE], uint32_t n)
{
    uint32_t mx = 0u;
    for (uint32_t k = 0; k < GD_HIST_N; k++) for (uint32_t i = 0; i < HS_FINE; i++) if (c[k][i] > mx) mx = c[k][i];
    const uint32_t s = hs_shift(mx);
    for (uint32_t k = 0; k < GD_HIST_N; k++) for (uint32_t i = 0; i < HS_FINE; i++) h->fine[k][i] = hs_scale(c[k][i], s);
    h->n = n; h->is_fine = 1u;
}
static inline void hs_clear(hs_t *h) { for (uint32_t k = 0; k < GD_HIST_N; k++) { for (uint32_t i = 0; i < HS_BINS; i++) h->bin[k][i] = 0u; for (uint32_t i = 0; i < HS_FINE; i++) h->fine[k][i] = 0u; } h->n = 0u; h->is_fine = 0u; }
static inline void hs_add(hs_t *h, uint16_t px)
{
    const uint32_t r5 = (px >> 11) & 31u, g6 = (px >> 5) & 63u, b5 = px & 31u;
    const uint32_t r8 = (r5 << 3) | (r5 >> 2), g8 = (g6 << 2) | (g6 >> 4), b8 = (b5 << 3) | (b5 >> 2);
#if BUG == 17
    const uint32_t y = (r8 + g8 + b8) / 3u;                                                                                          /* a plain average, not the weights */
#else
    const uint32_t y = (77u * r8 + 150u * g8 + 29u * b8) >> 8;
#endif
#if BUG == 18
    h->bin[0][y >> 3]++; h->bin[1][b5]++; h->bin[2][g6 >> 1]++; h->bin[3][r5]++;                                                    /* red and blue swapped */
#else
    h->bin[0][y >> 3]++; h->bin[1][r5]++; h->bin[2][g6 >> 1]++; h->bin[3][b5]++;
#endif
    h->n++;
}
/* Where the picture is inside its 96 x 72 thumbnail: the converter fits the picture into the thumbnail (never enlarged, rounded half to even) and centres it on black, so a landscape or portrait picture has black bars that
 * are NOT the picture and would pile up in the lowest bin. The rectangle [x0, x1) x [y0, y1) is that fit (the same integer rule as tgy.fit_rect, checked against it), taken one pixel in from each edge that borders a bar (the
 * resampling blends the edge, and a rounding difference of a pixel must not let a bar in). A picture with no size in the index, or larger than 65,535 pixels a side, counts the whole thumbnail. */
#define HS_TW 96u
#define HS_TH 72u
typedef struct { uint32_t x0, y0, x1, y1; } hs_rect_t;
static inline hs_rect_t hs_rect(uint32_t w, uint32_t h)
{
    hs_rect_t r = { 0u, 0u, HS_TW, HS_TH };
    if (w == 0u || h == 0u || w > 65535u || h > 65535u) return r;
    uint32_t rw, rh;
    if (w <= HS_TW && h <= HS_TH) { rw = w; rh = h; }
    else if (w * HS_TH >= h * HS_TW) {                                                                                              /* the width limits: 96 wide */
        const uint32_t num = h * HS_TW; uint32_t q = num / w; const uint32_t m = num % w;
#if BUG == 20
        if (2u * m >= w) q++;                                                                                                       /* ties round up, not to even */
#else
        if (2u * m > w || (2u * m == w && (q & 1u))) q++;
#endif
        rw = HS_TW; rh = q < 1u ? 1u : q > HS_TH ? HS_TH : q;
    } else {                                                                                                                        /* the height limits: 72 high */
        const uint32_t num = w * HS_TH; uint32_t q = num / h; const uint32_t m = num % h;
        if (2u * m > h || (2u * m == h && (q & 1u))) q++;
        rh = HS_TH; rw = q < 1u ? 1u : q > HS_TW ? HS_TW : q;
    }
    r.x0 = (HS_TW - rw) / 2u; r.y0 = (HS_TH - rh) / 2u; r.x1 = r.x0 + rw; r.y1 = r.y0 + rh;
#if BUG == 21
    return r;                                                                                                                       /* no inset: the edge pixels of a bar get in */
#else
    if (rw < HS_TW && rw > 2u) { r.x0++; r.x1--; }
    if (rh < HS_TH && rh > 2u) { r.y0++; r.y1--; }
    return r;
#endif
}
static inline int hs_inside(const hs_rect_t *r, uint32_t row, uint32_t col) { return row >= r->y0 && row < r->y1 && col >= r->x0 && col < r->x1; }
static inline uint32_t hs_max(const hs_t *h, uint32_t k) { uint32_t m = 0u; for (uint32_t i = 0; i < HS_BINS; i++) if (h->bin[k][i] > m) m = h->bin[k][i]; return m; }
static inline uint32_t hs_fmax(const hs_t *h, uint32_t k) { uint32_t m = 0u; for (uint32_t i = 0; i < HS_FINE; i++) if (h->fine[k][i] > m) m = h->fine[k][i]; return m; }
/* The bar of level i of histogram k as the page draws it: its count, and in how many pixels' width (a fine histogram is 1 pixel a level; a thumbnail's 32 bins are 8 pixels each, so both fill the 256 pixel plot). */
static inline uint32_t hs_bar(const hs_t *h, uint32_t k, uint32_t i) { return h->is_fine ? h->fine[k][i & 255u] : h->bin[k][(i & 255u) >> 3]; }
static inline uint32_t hs_plot_max(const hs_t *h, uint32_t k) { return h->is_fine ? hs_fmax(h, k) : hs_max(h, k); }
/* Height in pixels of a bar of `count` when the tallest bin is `max` and the plot is `hmax` pixels high. The height goes with the SQUARE ROOT of the count (owner, 2026-11-19: "some histograms still seem quite wrong"): a picture with
 * a black sky or a dark sea has more than half its pixels in one bin, and on a linear scale every other bin shrinks to a line. The counts are right; the plot hid them. */
static inline uint32_t hs_isqrt(uint32_t v) { uint32_t r = 0u, b = 1u << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static inline uint32_t hs_height(uint32_t count, uint32_t max, uint32_t hmax)
{
    if (count == 0u || max == 0u) return 0u;
#if BUG == 19
    const uint32_t v = (uint32_t)((uint64_t)hs_isqrt(count << 8) * hmax / hs_isqrt(max << 8));                                      /* a small bin vanishes */
    return v;
#elif BUG == 22
    const uint32_t v = (uint32_t)(((uint64_t)count * hmax) / max);                                                                   /* linear, as before */
    return v < 1u ? 1u : v;
#else
    const uint32_t v = (uint32_t)((uint64_t)hs_isqrt(count << 8) * hmax / hs_isqrt(max << 8));                                      /* 16 sqrt(count): the counts are at most 65,535 after the shift, so 8 bits of fraction fit */
    return v < 1u ? 1u : v;
#endif
}
/* Scales of a bar's height (owner, 2026-11-22: the histogram improvements): SQRT (the default, the page's first), LINEAR (what Photoshop draws) and LOG (every bin that has pixels is visible even beside a bin that holds half the picture). */
enum { HM_SQRT = 0, HM_LINEAR, HM_LOG, HM_N };
static const char *const hm_names[HM_N] = { "SQRT", "LINEAR", "LOG" };
static inline uint32_t hs_log2q(uint32_t v)                                                          /* 256 x log2(v) for v >= 1, linear between powers of two (error under 0.09) */
{
    uint32_t l = 0u; while ((v >> (l + 1u)) != 0u) l++;
    return (l << 8) | (l >= 8u ? (v >> (l - 8u)) & 255u : (v << (8u - l)) & 255u);
}
static inline uint32_t hs_height_m(uint32_t count, uint32_t max, uint32_t hmax, uint32_t mode)
{
    if (count == 0u || max == 0u) return 0u;
    if (mode == HM_LINEAR) { const uint32_t v = (uint32_t)(((uint64_t)count * hmax) / max); return v < 1u ? 1u : v; }
    if (mode == HM_LOG) { const uint32_t a = hs_log2q(count + 1u), b = hs_log2q(max + 1u), v = b ? (uint32_t)((uint64_t)a * hmax / b) : 0u; return v < 1u ? 1u : v; }
    return hs_height(count, max, hmax);
}
/* What the luma histogram says in numbers: the mean level (0..255) and the shares of black (level 0) and white (level 255) in thousandths; the shares only when the decoder counted every level. */
typedef struct { uint32_t mean, black_pm, white_pm; int exact; } hs_stats_t;
static inline void hs_stats(const hs_t *h, hs_stats_t *o)
{
    uint64_t tot = 0u, sum = 0u; for (uint32_t i = 0; i < 256u; i++) { const uint32_t c = hs_bar(h, 0u, i); tot += c; sum += (uint64_t)c * i; }
    o->mean = tot ? (uint32_t)(sum / tot) : 0u; o->exact = h->is_fine != 0u;
    o->black_pm = (tot && o->exact) ? (uint32_t)((uint64_t)h->fine[0][0] * 1000u / tot) : 0u; o->white_pm = (tot && o->exact) ? (uint32_t)((uint64_t)h->fine[0][255] * 1000u / tot) : 0u;
}
/* One key edge: returns 1 when the page should close. */
#if BUG == 13
static inline int gd_input(uint32_t edge) { return (edge & GP_KEY_B) != 0; }                                                         /* Select does not close */
#else
static inline int gd_input(uint32_t edge) { return (edge & (GP_KEY_B | GP_KEY_SELECT)) != 0; }
#endif
#endif
