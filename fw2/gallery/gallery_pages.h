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
    { "ZOOMED IN",      ""                    },
    { "D-PAD",          "PAN"                 },
    { "START",          "MAP: JUMP TO A SPOT" },
    { "L1 / R1",        "ZOOM OUT / IN"       },
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

/* ---- Settings: one setting in this slice, RESUME (the grid opens on the last picture). Left, Right and A change the value; B goes back to the menu. ---- */
/* One key edge: `resume` is the live flag (0 or 1); returns 1 when the page should close. */
static inline int gsp_input(uint32_t *resume, uint32_t edge)
{
    if (edge & (GP_KEY_LEFT | GP_KEY_A | (1u << 3))) {                                           /* bit 3 is Right */
#if BUG == 7
        (void)resume;                                                                            /* nothing changes */
#else
        *resume = (*resume & 1u) ^ 1u;
#endif
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
#define GI_ROWS 11u
typedef struct { uint32_t core_version, count, view_caps, open_ms, words, lost; const char *err; uint32_t set_layout, set_loads, set_stores, zoom_ms, land_ms, copy_ms, flip_ms; } gi_t;      /* view_caps: R_VIEW_CAPS (bit 1 = the 800 x 720 viewer); err: the last status line, 0 = none */
typedef struct { const char *label; char value[28]; } gi_row_t;
static inline char *gi_s(char *q, const char *p) { while (*p) *q++ = *p++; return q; }
static inline char *gi_u(char *q, uint32_t v) { char t[10]; uint32_t n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *q++ = t[--n]; return q; }
static inline uint32_t gi_cap(uint32_t v, uint32_t m) { return v > m ? m : v; }
static inline char *gi_hex8(char *q, uint32_t v) { for (int i = 28; i >= 0; i -= 4) *q++ = "0123456789ABCDEF"[(v >> i) & 15u]; return q; }
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
}
/* One key edge: returns 1 when the page should close (back to the menu). */
static inline int gi_input(uint32_t edge) { return (edge & (GP_KEY_LEFT | GP_KEY_B)) != 0; }

/* ---- Details: one picture's information and its histograms (a PLACEHOLDER page: the rows that need data the firmware does not read yet say NOT READ YET, and the four histogram frames are empty).
 * Select opens it from the grid (the selected picture) and from the picture view (not while zoomed in: Select + Left / Right is the zoomed view's previous / next); B or Select closes it. ---- */
#define GD_ROWS 8u
#define GD_HIST_N 4u
#define GD_NOT_READ "NOT READ YET"
#define GD_HIST_NOTE "NOT COMPUTED YET"
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
    r[4].label = "FORMAT";   q = gi_s(r[4].value, GD_NOT_READ); *q = 0;
    r[5].label = "CAMERA";   q = gi_s(r[5].value, GD_NOT_READ); *q = 0;
    r[6].label = "TAKEN";    q = gi_s(r[6].value, GD_NOT_READ); *q = 0;
    r[7].label = "EXPOSURE"; q = gi_s(r[7].value, GD_NOT_READ); *q = 0;
}
/* One key edge: returns 1 when the page should close. */
#if BUG == 13
static inline int gd_input(uint32_t edge) { return (edge & GP_KEY_B) != 0; }                                                         /* Select does not close */
#else
static inline int gd_input(uint32_t edge) { return (edge & (GP_KEY_B | GP_KEY_SELECT)) != 0; }
#endif
#endif
