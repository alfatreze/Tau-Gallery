#pragma GCC optimize ("Os")
/* Tau Gallery M0 test firmware (docs/M0_DESIGN.md 6c): the 800 x 720 viewer, the mode switch, landing reads and the interrupt block on a Pocket, with every number on screen for a screenshot.
 * No audio, no cold image: a small hot-RAM program on the Gallery bitstream (the macro bundle of tools/gallery_m0b_qsf_append.txt). Build: bash fw2/build_gallery.sh. */
#include "../platform/platform.h"
#include "../gfx/gfx.h"
#include "gallery_hw.h"

#define BG  RGB565(0, 0, 0)
#define FG  RGB565(255, 255, 255)
#define DIM RGB565(150, 150, 150)
#define ACC RGB565(255, 170, 40)
#define BAD RGB565(255, 80, 80)
#define GOOD RGB565(90, 220, 110)
#define LINE_H 20u
enum { M_RAMP = 0, M_GRAD, M_CHECK, M_TEX, M_SWITCH, M_LAND, M_IRQ, M_N };
static const char *const mname[M_N] = { "VIEWER RAMP", "VIEWER GRADIENT", "VIEWER CHECKER", "VIEWER TEXTURE", "MODE SWITCH X20", "LANDING READS", "INTERRUPTS" };

static char *put_u(char *q, uint32_t v) { char t[10]; uint32_t n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *q++ = t[--n]; return q; }
static char *put_s(char *q, const char *p) { while (*p) *q++ = *p++; return q; }
static char *put_h(char *q, uint32_t v) { for (int s = 28; s >= 0; s -= 4) *q++ = "0123456789ABCDEF"[(v >> s) & 15u]; return q; }
static uint32_t ms_of(uint32_t cyc) { return cyc / (CLK_HZ / 1000u); }

/* The result lines, kept on the page: one per kind of test, the landing reads one per size. */
#define N_RES 8u
#define ROW_IRQ 7u
static char res[N_RES][44]; static uint8_t res_bad[N_RES];
static void res_set(uint32_t i, const char *s, uint32_t bad) { char *q = res[i]; while (*s && q < res[i] + 43) *q++ = *s++; *q = 0; res_bad[i] = (uint8_t)bad; }
static uint32_t sel, filled_pat = 0xFFFFFFFFu;

static void page(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(10, 6, "TAU GALLERY M0 TEST", ACC, BG);
    { char b[40], *q = put_s(b, "CAPS "); q = put_h(q, REG_RD(R_VIEW_CAPS)); q = put_s(q, " SW "); q = put_u(q, gal_vid_switches()); *q = 0; gfx_text(10, 26, b, DIM, BG); }
    for (uint32_t i = 0; i < M_N; i++) gfx_text(10, 48u + i * 18u, mname[i], i == sel ? FG : DIM, BG);
    gfx_text(GFX_W - 26, 48u + sel * 18u, "<", ACC, BG);
    for (uint32_t i = 0; i < N_RES; i++) if (res[i][0]) gfx_text(10, 178u + i * LINE_H, res[i], res_bad[i] ? BAD : GOOD, BG);
    gfx_text(10, GFX_H - 20u, "UP DOWN  A RUN  B BACK", DIM, BG);
}
static void note(const char *s) { gfx_rect(0, 26, GFX_W, 20, BG); gfx_text(10, 26, s, ACC, BG); }
static void fill_progress(uint32_t y) { char b[24], *q = put_s(b, "FILLING "); q = put_u(q, y); q = put_s(q, " OF 720"); *q = 0; note(b); }

/* Fill the plane with a pattern (the UI stays on screen) and show it; returns 0 when it is on. */
static int show(uint32_t pat)
{
    char b[44], *q;
    const uint32_t t0 = cycles();
    if (filled_pat != pat) {
        const uint32_t bad = gal_fill(pat, fill_progress);
        q = put_s(b, "FILL "); q = put_s(q, gal_pattern_name[pat]); q = put_s(q, " "); q = put_u(q, ms_of(cycles() - t0)); q = put_s(q, " MS");
        if (bad) { q = put_s(q, " FAILED "); q = put_u(q, bad); }
        *q = 0; res_set(0, b, bad != 0u);
        if (bad) { filled_pat = 0xFFFFFFFFu; return 1; }
        filled_pat = pat;
    }
    const uint32_t d = gal_view_on();
    q = put_s(b, "VIEWER ON "); if (d) { q = put_u(q, ms_of(d)); q = put_s(q, " MS"); } else q = put_s(q, "TIMEOUT"); *q = 0; res_set(1, b, d == 0u);
    return d == 0u;
}

static void run_switch(void)
{
    if (filled_pat > GP_COUNT && show(GP_RAMP)) return;
    if (gal_view_off() == 0u) { res_set(2, "SWITCH BACK TIMEOUT", 1); return; }
    note("SWITCHING X20");
    gal_sw_t r; gal_switch_test(&r, 20u, 150u);
    char b[44], *q = put_s(b, "SWITCH "); q = put_u(q, r.ok); q = put_s(q, "/"); q = put_u(q, r.n); q = put_s(q, " DONE "); q = put_u(q, r.done);
    q = put_s(q, " WORST "); q = put_u(q, ms_of(r.worst_cyc)); q = put_s(q, " MS"); *q = 0;
    res_set(2, b, r.ok != r.n || r.done != r.n);
}

static void run_land(void)
{
    note("LANDING READS");
    for (uint32_t k = 0; k < GAL_LAND_KINDS; k++) {
        gal_land_t r; gal_land_test(&r, gal_land_len[k]);
        char b[44], *q = put_u(b, r.len / 1024u); q = put_s(q, "K ");
        if (!r.started) q = put_s(q, "NOT RUN");
        else if (!r.cmd_ok) { q = put_s(q, "READ FAILED "); q = put_u(q, ms_of(r.cmd_cyc)); q = put_s(q, " MS"); }
        else {
            q = put_u(q, gal_kbps(r.len, r.cmd_cyc)); q = put_s(q, " KB/S W"); q = put_u(q, r.words); q = put_s(q, " O"); q = put_u(q, r.ovf);
            q = put_s(q, r.idle_ok ? "" : " BUSY"); q = put_s(q, r.bad ? " BAD " : " OK "); q = put_u(q, r.bad == 0xFFFFFFFFu ? 0u : r.bad);
        }
        *q = 0;
        res_set(3u + k, b, !r.started || !r.cmd_ok || !r.idle_ok || r.ovf || r.bad || r.words != r.len / 4u);
    }
}

static void run_irq(void)
{
    if (filled_pat > GP_COUNT && show(GP_RAMP)) return;
    if (gal_view_off() == 0u) { res_set(2, "SWITCH BACK TIMEOUT", 1); return; }
    note("INTERRUPTS");
    gal_irq_t r; gal_irq_test(&r);
    char b[44], *q = put_s(b, "IRQ V"); q = put_u(q, r.vsync); q = put_s(q, " C"); q = put_u(q, r.cmd); q = put_s(q, "/"); q = put_u(q, r.cmd_n);
    q = put_s(q, " S"); q = put_u(q, r.sw); q = put_s(q, "/"); q = put_u(q, r.sw_n); *q = 0;
    res_set(ROW_IRQ, b, r.vsync < 55u || r.vsync > 65u || r.cmd != r.cmd_n || r.sw != r.sw_n);
}

static void viewer_loop(uint32_t pat)
{
    uint32_t prev = REG(R_INPUT) & 0xFFFFu;
    for (;;) {
        const uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge & KEY_B) { gal_view_off(); return; }
        if (edge & (KEY_LEFT | KEY_RIGHT)) {
            gal_view_off();
            pat = (edge & KEY_RIGHT) ? (pat + 1u) % GP_COUNT : (pat + GP_COUNT - 1u) % GP_COUNT; sel = pat;
            page(); if (show(pat)) return;
        }
    }
}

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    platform_probe();
    page();
    uint32_t prev = 0;
    for (;;) {
        const uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge & KEY_UP) { sel = sel ? sel - 1u : M_N - 1u; page(); }
        if (edge & KEY_DOWN) { sel = (sel + 1u) % M_N; page(); }
        if (edge & KEY_A) {
            if (sel <= M_TEX) { page(); if (!show(sel)) { viewer_loop(sel); } }
            else if (sel == M_SWITCH) run_switch();
            else if (sel == M_LAND) run_land();
            else run_irq();
            page();
        }
    }
}
