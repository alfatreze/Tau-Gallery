/* Host test of fw2/gallery/gallery_pages.h (menu, Controls list, Help pages). Compile with -DBUG=n for the mutants (1..4 must fail). */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../fw2/gallery/gallery_pages.h"
static int fails;
static void chk(const char *w, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
int main(void)
{
    gm_t m; gm_open(&m);
    chk("menu opens on the first entry", m.sel == GM_CONTROLS);
    chk("up from the first entry wraps to the last", gm_input(&m, GP_KEY_UP) == GP_STAY && m.sel == GM_COUNT - 1u);
    chk("down from the last entry wraps to the first", gm_input(&m, GP_KEY_DOWN) == GP_STAY && m.sel == 0u);
    chk("A on Controls opens Controls", gm_input(&m, GP_KEY_A) == GP_OPEN_CONTROLS);
    gm_input(&m, GP_KEY_DOWN);
    chk("A on Help opens Help", m.sel == GM_HELP && gm_input(&m, GP_KEY_A) == GP_OPEN_HELP);
    gm_input(&m, GP_KEY_DOWN);
    chk("A on Info opens Info", m.sel == GM_INFO && gm_input(&m, GP_KEY_A) == GP_OPEN_INFO);
    gm_input(&m, GP_KEY_DOWN);
    chk("A on Settings opens Settings", m.sel == GM_SETTINGS && gm_input(&m, GP_KEY_A) == GP_OPEN_SETTINGS);
    gm_input(&m, GP_KEY_DOWN);
    chk("A on Theme opens Theme", m.sel == GM_THEME && gm_input(&m, GP_KEY_A) == GP_OPEN_THEME);
    chk("down from Theme wraps to Controls", gm_input(&m, GP_KEY_DOWN) == GP_STAY && m.sel == GM_CONTROLS);
    chk("B closes the menu", gm_input(&m, GP_KEY_B) == GP_CLOSE);
    chk("Start closes the menu", gm_input(&m, GP_KEY_START) == GP_CLOSE);
    chk("Left and X do nothing in the menu", gm_input(&m, GP_KEY_LEFT) == GP_STAY && gm_input(&m, 1u << 6) == GP_STAY);

    gc_t c; gc_open(&c);
    chk("controls open at the top", c.sel == 0 && c.top == 0);
    int win = 1;
    for (uint32_t i = 0; i < 3u * GC_N; i++) { gc_input(&c, GP_KEY_DOWN); if (!(c.top <= c.sel && c.sel < c.top + GC_VIS)) win = 0; }
    chk("scrolling down keeps the selection inside the window (3 laps)", win);
    gc_open(&c);
    chk("up from the top wraps to the last row and shows the end", gc_input(&c, GP_KEY_UP) == 0 && c.sel == GC_N - 1u && c.top == GC_N - GC_VIS);
    chk("down from the last row wraps to the top", gc_input(&c, GP_KEY_DOWN) == 0 && c.sel == 0u && c.top == 0u);
    chk("B and Left leave Controls, up and down do not", gc_input(&c, GP_KEY_B) == 1 && gc_input(&c, GP_KEY_LEFT) == 1 && gc_input(&c, GP_KEY_UP) == 0);
    chk("the list is longer than the window (the scroll is used)", GC_N > GC_VIS);

    gh_t h; gh_open(&h);
    chk("help opens on page 1", h.page == 0);
    chk("down turns the page", gh_input(&h, GP_KEY_DOWN) == 0 && h.page == 1);
    gh_input(&h, GP_KEY_DOWN); chk("down from the last page wraps to the first", gh_input(&h, GP_KEY_DOWN) == 0 && h.page == 0);
    chk("up from the first page wraps to the last", gh_input(&h, GP_KEY_UP) == 0 && h.page == GH_PAGES - 1u);
    chk("B and Left close Help", gh_input(&h, GP_KEY_B) == 1 && gh_input(&h, GP_KEY_LEFT) == 1);

    {
        gi_row_t r[GI_ROWS]; gi_t d = { 0x4D50331Bu, 18u, 0x80000003u, GI_NONE, 0u, 0u, 0 };
        gi_format(&d, r);
        chk("info: core version is 8 hex digits", !strcmp(r[0].label, "CORE VERSION") && !strcmp(r[0].value, "4D50331B"));
        chk("info: picture count", !strcmp(r[1].value, "18"));
        chk("info: viewer present (caps bit 1)", !strcmp(r[2].value, "800 X 720"));
        chk("info: before any open", !strcmp(r[3].value, "NONE YET") && !strcmp(r[4].value, "NONE YET") && !strcmp(r[5].value, "NONE"));
        d.core_version = 0xABCDu; d.view_caps = 0x80000001u; d.open_ms = 1234u; d.words = 432128u; d.lost = 3u; d.err = "OPEN FAILED"; gi_format(&d, r);
        chk("info: leading zeros of the version", !strcmp(r[0].value, "0000ABCD"));
        chk("info: no viewer when only the sequencer bit is set", !strcmp(r[2].value, "NO VIEWER"));
        chk("info: open time in ms", !strcmp(r[3].value, "1234 MS"));
        chk("info: words and lost words", !strcmp(r[4].value, "432128 WORDS, 3 LOST"));
        chk("info: the last error text", !strcmp(r[5].value, "OPEN FAILED"));
        d.open_ms = 0u; gi_format(&d, r); chk("info: 0 ms is a time, not 'none yet'", !strcmp(r[3].value, "0 MS"));
        int fit = 1; for (uint32_t i = 0; i < GI_ROWS; i++) if (strlen(r[i].label) > 14u || strlen(r[i].value) > 27u) fit = 0;
        chk("info: labels (14) and values (27) fit", fit);
        d.set_layout = 1u; d.set_loads = 3u; d.set_stores = 2u; gi_format(&d, r);
        chk("info: the settings row shows layout, loads and saves", !strcmp(r[6].label, "SETTINGS") && !strcmp(r[6].value, "L1 LOADED 3 SAVED 2"));
        d.set_layout = 99u; d.set_loads = 4000000000u; d.set_stores = 4000000000u; gi_format(&d, r);
        chk("info: huge counters are capped, the row still fits", !strcmp(r[6].value, "L15 LOADED 9999 SAVED 9999") && strlen(r[6].value) <= 27u);
        d.zoom_ms = GI_NONE; d.land_ms = GI_NONE; d.copy_ms = GI_NONE; gi_format(&d, r);
        chk("info: before any zoom the three zoom rows say NONE YET", !strcmp(r[7].label, "ZOOM OPEN") && !strcmp(r[8].label, "ZOOM LAND") && !strcmp(r[9].label, "ZOOM COPY") && !strcmp(r[7].value, "NONE YET") && !strcmp(r[8].value, "NONE YET") && !strcmp(r[9].value, "NONE YET"));
        d.zoom_ms = 850u; d.land_ms = 31u; d.copy_ms = 6u; gi_format(&d, r);
        chk("info: the zoom timings, each in its own row", !strcmp(r[7].value, "850 MS") && !strcmp(r[8].value, "31 MS") && !strcmp(r[9].value, "6 MS"));
        d.flip_ms = GI_NONE; gi_format(&d, r); chk("info: ZOOM FLIP says NONE YET before any re-centre", !strcmp(r[10].label, "ZOOM FLIP") && !strcmp(r[10].value, "NONE YET"));
        d.flip_ms = 120u; gi_format(&d, r); chk("info: ZOOM FLIP shows the re-centre time", !strcmp(r[10].value, "120 MS"));
        d.zoom_ms = 0u; d.land_ms = 0u; d.copy_ms = 0u; gi_format(&d, r);
        chk("info: 0 ms is a time, not 'none yet'", !strcmp(r[7].value, "0 MS") && !strcmp(r[8].value, "0 MS") && !strcmp(r[9].value, "0 MS"));
        d.zoom_ms = 4000000000u; gi_format(&d, r); chk("info: a huge time still fits", strlen(r[7].value) <= 27u);
        { gi_t z = { 0x4D50331Eu, 52u, 0x80000003u, 257u, 29696u, 0u, 0 };                                                                          /* the version 1 zoom statistics */
          gi_format(&z, r); chk("info: tile rows say NONE YET before any tile", !strcmp(r[11].label, "TILES READ") && !strcmp(r[11].value, "NONE YET") && !strcmp(r[12].label, "TILES DECODED") && !strcmp(r[12].value, "NONE YET") && !strcmp(r[16].label, "LONGEST STALL") && !strcmp(r[16].value, "NONE YET") && !strcmp(r[17].label, "ZOOM PIPELINE") && !strcmp(r[17].value, "ON") && !strcmp(r[13].label, "TILE DECODE") && !strcmp(r[13].value, "NONE YET") && !strcmp(r[14].label, "LONGEST PASS") && !strcmp(r[14].value, "NONE YET") && !strcmp(r[15].label, "PAN STALLS") && !strcmp(r[15].value, "NONE YET"));
          z.zt_n = 3u; z.zt_land_sum = 12000u; z.zt_land_max = 9500u; z.zt_dec_n = 3u; z.zt_dec_sum = 6000u; z.zt_dec_max = 3100u; z.zt_pass_max = 21400u; z.zt_catch = 340u; z.zt_moved = 328u; gi_format(&z, r);
          chk("info: TILES READ shows the count, the average and the maximum of the card read", !strcmp(r[11].value, "3 AVG 4.0 MAX 9.5 MS"));
          chk("info: TILE DECODE shows the average and the maximum", !strcmp(r[13].value, "AVG 2.0 MAX 3.1 MS"));
          z.zt_dec_n = 2u; gi_format(&z, r); chk("info: the decode average is over the decodes, not the reads (6,000 us over 2 is 3.0 ms)", !strcmp(r[13].value, "AVG 3.0 MAX 3.1 MS")); z.zt_dec_n = 0u; gi_format(&z, r); chk("info: no decode yet says NONE YET even when tiles were read", !strcmp(r[13].value, "NONE YET")); z.zt_dec_n = 3u;
          chk("info: LONGEST PASS in milliseconds with a decimal", !strcmp(r[14].value, "21.4 MS"));
          chk("info: PAN STALLS is the frames behind minus the frames that moved", !strcmp(r[15].value, "12 OF 340 FRAMES"));
          z.zt_n = 9u; z.zt_dec_n = 7u; z.zt_stall_run_max = 4u; gi_format(&z, r);
          chk("info: TILES DECODED is the decodes done and the reads that were never used", !strcmp(r[12].label, "TILES DECODED") && !strcmp(r[12].value, "7 DONE, 2 UNUSED"));
          z.zt_n = 3u; z.zt_dec_n = 5u; gi_format(&z, r); chk("info: fewer reads than decodes (a stream used twice) is 0 unused, not a huge number", !strcmp(r[12].value, "5 DONE, 0 UNUSED")); z.zt_n = 3u; z.zt_dec_n = 3u;
          chk("info: LONGEST STALL is the longest run of stalled frames", !strcmp(r[16].label, "LONGEST STALL") && !strcmp(r[16].value, "4 FRAMES IN A ROW")); z.zt_stall_run_max = 1u; gi_format(&z, r); chk("info: one frame is singular", !strcmp(r[16].value, "1 FRAME IN A ROW")); z.zt_stall_run_max = 4u;
          z.pipe_off = 1u; gi_format(&z, r); chk("info: ZOOM PIPELINE says OFF when it is switched off", !strcmp(r[17].value, "OFF")); z.pipe_off = 0u; gi_format(&z, r); chk("info: and ON otherwise", !strcmp(r[17].value, "ON"));
          gi_format(&z, r); chk("info: PASS SPLIT is NONE YET with no pass timed", (z.zt_pass_max = 0u, gi_format(&z, r), !strcmp(r[18].label, "PASS SPLIT") && !strcmp(r[18].value, "NONE YET"))); z.zt_pass_max = 21400u;
          z.zt_sp = 1200u; z.zt_sc = 0u; z.zt_sn = 2400u; z.zt_sj = 16500u; z.zt_ss = 1300u; gi_format(&z, r);
          chk("info: PASS SPLIT shows plan, copy, need, job and show in whole milliseconds", !strcmp(r[18].value, "P1 C0 N2 J17 S1 MS"));
          z.zt_sj = 499u; gi_format(&z, r); chk("info: a split under half a millisecond rounds to 0, 500 up to 1", !strcmp(r[18].value, "P1 C0 N2 J0 S1 MS") && (z.zt_sj = 500u, gi_format(&z, r), !strcmp(r[18].value, "P1 C0 N2 J1 S1 MS")));
          z.zt_sj = 4000000000u; gi_format(&z, r); chk("info: a huge part is capped at 99", !strcmp(r[18].value, "P1 C0 N2 J99 S1 MS")); z.zt_sj = 16500u;
          z.zt_land_sum = 14999u; z.zt_n = 3u; gi_format(&z, r); chk("info: the average is cut, not rounded (4.9 for 4,999 microseconds)", !strcmp(r[11].value, "3 AVG 4.9 MAX 9.5 MS"));
          z.zt_moved = 400u; gi_format(&z, r); chk("info: more frames moved than were behind (cannot be) gives 0 stalls, not a huge number", !strcmp(r[15].value, "0 OF 340 FRAMES"));
          z.zt_n = 100000u; z.zt_land_sum = 400000000u; z.zt_land_max = 4000000000u; gi_format(&z, r); chk("info: the tile count is capped at 9999 so the row fits", !strcmp(r[11].value, "9999 AVG 4.0 MAX 999.9 MS"));
          z.zt_n = 4000000000u; z.zt_land_sum = 4000000000u; z.zt_land_max = 4000000000u; z.zt_sp = z.zt_sc = z.zt_sn = z.zt_sj = z.zt_ss = 4000000000u; z.zt_dec_n = 4000000000u; z.zt_dec_sum = 4000000000u; z.zt_dec_max = 4000000000u; z.zt_pass_max = 4000000000u; z.zt_catch = 4000000000u; z.zt_moved = 0u; gi_format(&z, r);
          int fits = 1; for (uint32_t i = 0; i < GI_ROWS; i++) if (strlen(r[i].label) > 14u || strlen(r[i].value) > 27u) fits = 0;
          chk("info: the largest values still fit the screen (label 14, value 27)", fits && !strcmp(r[14].value, "999.9 MS") && !strcmp(r[15].value, "99999 OF 99999 FRAMES")); }
        chk("info: A asks to switch the zoom pipeline (2), B and Left close (1)", gi_input(GP_KEY_A) == 2 && gi_input(GP_KEY_B) == 1 && gi_input(GP_KEY_LEFT) == 1);
        chk("info: B and Left close, the other keys do not (A is the pipeline switch now)", gi_input(GP_KEY_B) == 1 && gi_input(GP_KEY_LEFT) == 1 && gi_input(GP_KEY_UP) == 0 && gi_input(GP_KEY_START) == 0 && gi_input(GP_KEY_DOWN) == 0);
    }
    {
        uint32_t rs = 1u, gr = 0u, dt = 1u, lt = 0u; gsp_t sp = { 0 };
        chk("settings: A turns resume off", gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_A) == 0 && rs == 0u && gr == 0u && dt == 1u);
        chk("settings: Left turns it on", gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && rs == 1u);
        chk("settings: Right turns it off", gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && rs == 0u);
        rs = 5u; gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_A); chk("settings: the flag stays 0 or 1 whatever it held", rs == 0u);
        chk("settings: B closes and changes nothing", gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_B) == 1 && rs == 0u && gr == 0u && dt == 1u);
        chk("settings: Start neither changes nor closes", gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_START) == 0 && rs == 0u);
        chk("settings: Down moves to GRID SIZE (no value changes), DITHER, COLOUR, Down wraps to RESUME, Up wraps to COLOUR", gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_DOWN) == 0 && sp.row == GSP_GRID && rs == 0u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_DOWN) == 0 && sp.row == GSP_DITHER
            && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_DOWN) == 0 && sp.row == GSP_COLOUR && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_DOWN) == 0 && sp.row == GSP_RESUME && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_UP) == 0 && sp.row == GSP_COLOUR && gr == 0u && dt == 1u && lt == 0u);
        chk("settings: on COLOUR Right goes 1..7 and wraps to 0, Left wraps to 7; nothing else changes", gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && lt == 1u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_A) == 0 && lt == 2u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && lt == 1u
            && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && lt == 0u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && lt == 7u && gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && lt == 0u && gr == 0u && dt == 1u && rs == 0u);
        lt = 12u; gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3); chk("settings: a colour value out of range comes back into 0..7 (Right)", lt < 8u); lt = 12u; gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT); chk("settings: a colour value out of range comes back into 0..7 (Left)", lt < 8u); lt = 0u;
        sp.row = GSP_GRID;
        chk("settings: on GRID SIZE Right goes 0, 1, 2, 3 and wraps to 0; Left wraps to 3", gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && gr == 1u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_A) == 0 && gr == 2u && gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && gr == 3u
            && gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && gr == 0u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && gr == 3u && rs == 0u && dt == 1u);
        gr = 9u; gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3); chk("settings: a grid value out of range comes back into 0..3", gr < 4u);
        sp.row = GSP_DITHER; gr = 2u;
        chk("settings: on DITHER Right goes 1, 2, 0 (off), Left goes back; the grid stays", gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && dt == 2u && gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3) == 0 && dt == 0u && gsp_input(&sp, &rs, &gr, &dt, &lt, GP_KEY_LEFT) == 0 && dt == 2u && gr == 2u);
        dt = 7u; gsp_input(&sp, &rs, &gr, &dt, &lt, 1u << 3); chk("settings: a dither value out of range comes back into 0..2", dt < 3u);
        chk("settings: the names of the choices", !strcmp(gsp_grid_names[0], "4 X 3") && !strcmp(gsp_grid_names[3], "8 X 6") && !strcmp(gsp_dither_names[0], "OFF") && !strcmp(gsp_dither_names[2], "MOVING"));
    }
    {
        gt_t g; gt_open(&g); gt_look_t l = { 0u, 0u, 1u };
        chk("theme page opens on the THEME row", g.sel == GT_THEME);
        chk("theme: Right goes to the next theme", gt_input(&g, &l, 2u, 1u << 3) == GT_CHANGED && l.theme == 1u);
        chk("theme: Right wraps after the last theme", gt_input(&g, &l, 2u, 1u << 3) == GT_CHANGED && l.theme == 0u);
        chk("theme: Left wraps before the first theme", gt_input(&g, &l, 2u, GP_KEY_LEFT) == GT_CHANGED && l.theme == 1u);
        chk("theme: Down chooses MODE", gt_input(&g, &l, 2u, GP_KEY_DOWN) == GT_STAY && g.sel == GT_MODE);
        chk("theme: the mode toggles with Right and Left", gt_input(&g, &l, 2u, 1u << 3) == GT_CHANGED && l.pol == 1u && gt_input(&g, &l, 2u, GP_KEY_LEFT) == GT_CHANGED && l.pol == 0u);
        gt_input(&g, &l, 2u, GP_KEY_DOWN);
        chk("theme: the accent row changes only the accent", g.sel == GT_ACCENT && gt_input(&g, &l, 2u, 1u << 3) == GT_CHANGED && l.pal == 2u && l.theme == 1u && l.pol == 0u);
        l.pal = TC_PALETTE_N - 1u; gt_input(&g, &l, 2u, 1u << 3); chk("theme: the accent wraps over the 19 colours", l.pal == 0u);
        gt_input(&g, &l, 2u, GP_KEY_LEFT); chk("theme: the accent wraps backwards", l.pal == TC_PALETTE_N - 1u);
        gt_input(&g, &l, 2u, GP_KEY_DOWN); chk("theme: Down from the last row wraps to THEME", g.sel == GT_THEME);
        gt_input(&g, &l, 2u, GP_KEY_UP); chk("theme: Up from the first row wraps to ACCENT", g.sel == GT_ACCENT);
        chk("theme: B closes, A and Start do nothing", gt_input(&g, &l, 2u, GP_KEY_B) == GT_CLOSE && gt_input(&g, &l, 2u, GP_KEY_A) == GT_STAY && gt_input(&g, &l, 2u, GP_KEY_START) == GT_STAY);
        chk("theme: a build with one theme stays on theme 0", (l.theme = 0u, g.sel = GT_THEME, gt_input(&g, &l, 1u, 1u << 3) == GT_CHANGED && l.theme == 0u));
    }
    {   /* ---- Details ---- */
        gd_row_t r[GD_ROWS]; gd_t d = { "Sunset over the harbour", 4u, 52u, 2592768u, 2560u, 1705u };
        gd_format(&d, r);
        chk("details: the name is the picture's", !strcmp(r[0].label, "NAME") && !strcmp(r[0].value, "Sunset over the harbour"));
        chk("details: the position is 1-based (index 4 of 52 is picture 5)", !strcmp(r[1].label, "PICTURE") && !strcmp(r[1].value, "5 OF 52"));
        chk("details: the size is width X height", !strcmp(r[2].label, "SIZE") && !strcmp(r[2].value, "2560 X 1705"));
        chk("details: 2,592,768 bytes is 2.4 MB", !strcmp(r[3].label, "FILE") && !strcmp(r[3].value, "2.4 MB"));
        chk("details: gd_format fills the four rows every picture has (NAME, PICTURE, SIZE, FILE)", !strcmp(r[0].label, "NAME") && !strcmp(r[1].label, "PICTURE") && !strcmp(r[2].label, "SIZE") && !strcmp(r[3].label, "FILE"));
        char b[28]; gd_size(b, 0u) [0] = 0; chk("details: 0 bytes", !strcmp(b, "0 B")); *gd_size(b, 1023u) = 0; chk("details: 1023 bytes stays in bytes", !strcmp(b, "1023 B"));
        *gd_size(b, 1024u) = 0; chk("details: 1024 bytes is 1 KB", !strcmp(b, "1 KB")); *gd_size(b, 1048575u) = 0; chk("details: just under 1 MiB is 1023 KB", !strcmp(b, "1023 KB"));
        *gd_size(b, 1048576u) = 0; chk("details: 1 MiB is 1.0 MB", !strcmp(b, "1.0 MB")); *gd_size(b, 4294967295u) = 0; chk("details: the largest size does not overflow (4095.9 MB)", !strcmp(b, "4095.9 MB"));
        d.width = 0u; gd_format(&d, r); chk("details: a picture whose index has no size says UNKNOWN", !strcmp(r[2].value, "UNKNOWN"));
        d.width = 800u; d.height = 0u; gd_format(&d, r); chk("details: so does a missing height", !strcmp(r[2].value, "UNKNOWN"));
        gd_t longn = { "0123456789012345678901234567890123456789", 0u, 1u, 10u, 1u, 1u }; gd_format(&longn, r);
        chk("details: a long name is cut to the column (26) and ends", strlen(r[0].value) == 26u && !strncmp(r[0].value, "01234567890123456789012345", 26));
        { uint32_t okw = 1; gd_t w = { "N", 4294967294u, 4294967295u, 4294967295u, 4294967295u, 4294967295u }; gd_format(&w, r); for (uint32_t i = 0; i < 4u; i++) if (strlen(r[i].value) > 27u) okw = 0; chk("details: the widest values fit the 28 byte value field", okw); }
        {   /* Gallery 1.0 details: the items of a picture (bytes as tools/gallery/tgy.py pack_details writes them), the INFO and SHOT rows, the pages */
            static const uint8_t full[] = { 1, 4, 'J', 'P', 'E', 'G', 2, 12, 'C','a','n','o','n',' ','E','O','S',' ','R','5', 3, 14, 'R','F','2','4','-','7','0','m','m',' ','F','2','.','8', 4, 7, 0xE8, 0x07, 5, 17, 13, 4, 59,
                                            5, 8, 1, 0, 0, 0, 250, 0, 0, 0, 6, 2, 28, 0, 7, 4, 0x90, 0x01, 0, 0, 8, 2, 0x5E, 0x01 };
            gd_meta_t m; gd_row_t ir[GD_ROWS], sr[GD_ROWS]; gd_t dd = { "Sunset", 4u, 52u, 2592768u, 2560u, 1705u };
            gd_meta_parse(full, sizeof full, &m);
            chk("details meta: every field of a full run is found", m.has == (GDM_FORMAT | GDM_CAMERA | GDM_LENS | GDM_TAKEN | GDM_SHUTTER | GDM_APERTURE | GDM_ISO | GDM_FOCAL) && !strcmp(m.format, "JPEG") && !strcmp(m.camera, "Canon EOS R5") && !strcmp(m.lens, "RF24-70mm F2.8"));
            chk("details meta: date, shutter, aperture, ISO and focal length values", m.year == 2024u && m.mon == 5u && m.day == 17u && m.hour == 13u && m.min == 4u && m.sec == 59u && m.shutter_n == 1u && m.shutter_d == 250u && m.aperture10 == 28u && m.iso == 400u && m.focal10 == 350u);
            uint32_t n = gdi_rows(&dd, &m, ir);
            chk("details INFO: eight rows, the four always there and then FORMAT, CAMERA, LENS, TAKEN", n == 8u && !strcmp(ir[4].label, "FORMAT") && !strcmp(ir[4].value, "JPEG") && !strcmp(ir[5].label, "CAMERA") && !strcmp(ir[5].value, "Canon EOS R5") && !strcmp(ir[6].label, "LENS") && !strcmp(ir[6].value, "RF24-70mm F2.8") && !strcmp(ir[7].label, "TAKEN") && !strcmp(ir[7].value, "2024-05-17 13:04"));
            n = gds_rows(&m, sr);
            chk("details SHOT: shutter 1/250 S, aperture F/2.8, ISO 400, focal length 35 MM", n == 4u && !strcmp(sr[0].label, "SHUTTER") && !strcmp(sr[0].value, "1/250 S") && !strcmp(sr[1].label, "APERTURE") && !strcmp(sr[1].value, "F/2.8") && !strcmp(sr[2].label, "ISO") && !strcmp(sr[2].value, "400") && !strcmp(sr[3].label, "FOCAL LENGTH") && !strcmp(sr[3].value, "35 MM"));
            { static const uint8_t sh[] = { 5, 8, 2, 0, 0, 0, 1, 0, 0, 0, 8, 2, 0xAF, 0x00 }, sh2[] = { 5, 8, 5, 0, 0, 0, 2, 0, 0, 0 }, sh3[] = { 5, 8, 1, 0, 0, 0, 3, 0, 0, 0, 6, 2, 0xFF, 0x00 }; gd_meta_t q;
              gd_meta_parse(sh, sizeof sh, &q); n = gds_rows(&q, sr); chk("details SHOT: 2 S for a whole number of seconds, 17.5 MM for a fractional focal length", n == 2u && !strcmp(sr[0].value, "2 S") && !strcmp(sr[1].value, "17.5 MM"));
              gd_meta_parse(sh2, sizeof sh2, &q); n = gds_rows(&q, sr); chk("details SHOT: 5/2 is 2.5 S", n == 1u && !strcmp(sr[0].value, "2.5 S"));
              gd_meta_parse(sh3, sizeof sh3, &q); n = gds_rows(&q, sr); chk("details SHOT: 1/3 S and a three-digit aperture (F/25.5)", n == 2u && !strcmp(sr[0].value, "1/3 S") && !strcmp(sr[1].value, "F/25.5")); }
            { static const uint8_t none[] = { 99, 3, 1, 2, 3 }, bad_date[] = { 4, 7, 0xE8, 0x07, 13, 17, 13, 4, 59 }, zero_den[] = { 5, 8, 1, 0, 0, 0, 0, 0, 0, 0 }, wrong_size[] = { 7, 3, 1, 2, 3 }, over[] = { 7, 4, 0x90, 0x01, 0, 0, 2, 40, 'x', 'y' }, trunc[] = { 7 };
              gd_meta_t q;
              gd_meta_parse(none, sizeof none, &q); chk("details meta: an unknown tag is skipped and nothing is found", q.has == 0u);
              gd_meta_parse(bad_date, sizeof bad_date, &q); chk("details meta: a date with month 13 is not a date", !(q.has & GDM_TAKEN));
              gd_meta_parse(zero_den, sizeof zero_den, &q); chk("details meta: a shutter with a zero denominator is not a shutter", !(q.has & GDM_SHUTTER));
              gd_meta_parse(wrong_size, sizeof wrong_size, &q); chk("details meta: an ISO item of the wrong size is ignored", !(q.has & GDM_ISO));
              gd_meta_parse(over, sizeof over, &q); chk("details meta: an item reaching past the run ends the parse, what came before it counts", q.has == GDM_ISO && q.iso == 400u);
              gd_meta_parse(trunc, sizeof trunc, &q); chk("details meta: a run cut after a tag byte is harmless", q.has == 0u);
              gd_meta_parse(none, 0u, &q); chk("details meta: an empty run has nothing", q.has == 0u);
              n = gdi_rows(&dd, &q, ir); chk("details INFO: a picture with no details has exactly the four rows (no empty fields)", n == 4u);
              n = gds_rows(&q, sr); chk("details SHOT: no exposure fields, no rows", n == 0u); }
            { static uint8_t longcam[44]; gd_meta_t q; longcam[0] = 2; longcam[1] = 40; for (uint32_t i = 0; i < 40u; i++) longcam[2 + i] = 'x'; gd_meta_parse(longcam, sizeof longcam, &q); n = gdi_rows(&dd, &q, ir);
              chk("details INFO: a 40 character camera name is cut to the 27 character column", n == 5u && strlen(ir[4].value) == 26u); }
            { gdp_t p; gd_meta_t q; static const uint8_t isoonly[] = { 7, 4, 100, 0, 0, 0 }, fmtonly[] = { 1, 3, 'P', 'N', 'G' };
              gdp_open(&p, &m); chk("details pages: a full picture has INFO, SHOT, HISTOGRAMS, OVERLAY", p.n == 4u && p.id[0] == GDP_INFO && p.id[1] == GDP_SHOT && p.id[2] == GDP_HIST && p.id[3] == GDP_OVER && p.cur == 0u);
              gd_meta_parse(fmtonly, sizeof fmtonly, &q); gdp_open(&p, &q); chk("details pages: with no exposure field the SHOT page is not offered", p.n == 3u && p.id[0] == GDP_INFO && p.id[1] == GDP_HIST && p.id[2] == GDP_OVER);
              gd_meta_parse(isoonly, sizeof isoonly, &q); gdp_open(&p, &q); chk("details pages: one exposure field is enough to offer it", p.n == 4u);
              gdp_open(&p, &m); char b[12];
              gdp_indicator(b, &p); chk("details indicator: 1/4 on the first page", !strcmp(b, "1/4"));
              chk("details pages: Right turns to SHOT (2/4), HISTOGRAMS (3/4), OVERLAY (4/4) then wraps to INFO", gdp_input(&p, GP_KEY_RIGHT) == 0 && p.cur == 1u && (gdp_indicator(b, &p), !strcmp(b, "2/4")) && gdp_input(&p, GP_KEY_RIGHT) == 0 && p.cur == 2u && gdp_input(&p, GP_KEY_RIGHT) == 0 && p.cur == 3u && gdp_input(&p, GP_KEY_RIGHT) == 0 && p.cur == 0u);
              chk("details pages: Left from the first page wraps to the last", gdp_input(&p, GP_KEY_LEFT) == 0 && p.cur == 3u && gdp_input(&p, GP_KEY_LEFT) == 0 && p.cur == 2u);
              chk("details pages: B and Select close, Up, Down, A and Start do not, the page stays", gdp_input(&p, GP_KEY_B) == 1 && gdp_input(&p, GP_KEY_SELECT) == 1 && gdp_input(&p, GP_KEY_UP) == 0 && gdp_input(&p, GP_KEY_DOWN) == 0 && gdp_input(&p, GP_KEY_A) == 0 && gdp_input(&p, GP_KEY_START) == 0 && p.cur == 2u);
              gd_meta_parse(fmtonly, sizeof fmtonly, &q); gdp_open(&p, &q); gdp_input(&p, GP_KEY_RIGHT); gdp_input(&p, GP_KEY_RIGHT); gdp_indicator(b, &p); chk("details indicator: 3/3 on the last page of a three page picture", !strcmp(b, "3/3")); }
        }
        chk("details: B closes", gd_input(GP_KEY_B) == 1); chk("details: Select closes (it opens the page, so it toggles)", gd_input(GP_KEY_SELECT) == 1);
        chk("details: A, Up, Down, Left, Right and Start do not close", gd_input(GP_KEY_A) == 0 && gd_input(GP_KEY_UP) == 0 && gd_input(GP_KEY_DOWN) == 0 && gd_input(GP_KEY_LEFT) == 0 && gd_input(1u << 3) == 0 && gd_input(GP_KEY_START) == 0);
        chk("details: four histogram frames, in the order luma, red, green, blue", GD_HIST_N == 4u && !strcmp(gd_hist_labels[0], "LUMA") && !strcmp(gd_hist_labels[1], "RED") && !strcmp(gd_hist_labels[2], "GREEN") && !strcmp(gd_hist_labels[3], "BLUE"));
        chk("details: the histogram note fits a frame (16 characters)", strlen(GD_HIST_NOTE) <= 16u);
        { int okk = GD_KEYS_N == 6u; for (uint32_t i = 0; i < GD_KEYS_N; i++) if (strlen(gd_keys[i]) > 35u) okk = 0;                 /* 8 pixels a character from x 112 on a 400 pixel screen, 6 pixels margin */
          chk("details: the key reminder lines fit the screen beside the preview", okk);
          int fx = 0, ty = 0, kx = 0, ky = 0; for (uint32_t i = 0; i < GD_KEYS_N; i++) { if (!strncmp(gd_keys[i], "X ", 2) && strstr(gd_keys[i], "FILTER")) kx = 1; if (!strncmp(gd_keys[i], "Y ", 2) && strstr(gd_keys[i], "TURN")) ky = 1; }
          for (uint32_t i = 0; i < GC_N; i++) { if (!strcmp(gc_rows[i].key, "X") && strstr(gc_rows[i].what, "FILTER")) fx = 1; if (!strcmp(gc_rows[i].key, "Y") && strstr(gc_rows[i].what, "TURN")) ty = 1; }
          chk("details and Controls: X is the colour filter and Y the turn, in the reminder and in the Controls list", fx && ty && kx && ky); }
        { /* histograms: every RGB565 value against a separate, floating-point statement of the rule */
            int okb = 1; hs_t h;
            for (uint32_t px = 0; px < 65536u; px++) {
                hs_clear(&h); hs_add(&h, (uint16_t)px);
                const double R = ((px >> 11) & 31u) * 255.0 / 31.0, G = ((px >> 5) & 63u) * 255.0 / 63.0, B = (px & 31u) * 255.0 / 31.0;
                const uint32_t rb = (px >> 11) & 31u, gb = ((px >> 5) & 63u) >> 1, bb = px & 31u;
                const uint32_t yb = (uint32_t)((0.30078125 * R + 0.5859375 * G + 0.11328125 * B) / 8.0 + 1e-9);          /* 77/256, 150/256, 29/256 of the channels expanded to 255 (the integer expansion differs from this by under 1/255) */
                uint32_t hits = 0u; for (uint32_t k = 0; k < GD_HIST_N; k++) for (uint32_t i = 0; i < HS_BINS; i++) hits += h.bin[k][i];
                if (h.n != 1u || hits != 4u || h.bin[1][rb] != 1u || h.bin[2][gb] != 1u || h.bin[3][bb] != 1u) okb = 0;
                uint32_t yi = 99u; for (uint32_t i = 0; i < HS_BINS; i++) if (h.bin[0][i]) yi = i;
                if (yi > 31u || (yi != yb && yi + 1u != yb && yi != yb + 1u)) okb = 0;                                   /* the luma bin: the integer rule may sit one bin from the real-valued one at a bin edge, never more */
            }
            chk("histogram: each of the 65,536 pixel values lands in one bin per histogram (red and blue their 5 bits, green its top 5, luma within a bin of the weighted sum)", okb);
            hs_clear(&h); hs_add(&h, 0xFFFFu); hs_add(&h, 0x0000u); hs_add(&h, 0xF800u); hs_add(&h, 0x07E0u); hs_add(&h, 0x001Fu);
            chk("histogram: white is the last luma bin and black the first; pure red counts in the last red bin and none in green or blue", h.bin[0][31] >= 1u && h.bin[0][0] >= 1u && h.bin[1][31] == 2u && h.bin[1][0] == 3u && h.bin[2][31] == 2u && h.bin[3][31] == 2u && h.n == 5u);
            { static uint32_t c[4][256]; memset(c, 0, sizeof c);
              c[0][3] = 70000u; c[1][5] = 1u; c[2][200] = 140000u; c[3][9] = 3u; c[3][10] = 65535u; hs_from_counts(&h, c, 213539u);
              chk("histogram: decoder counts above 65,535 are shifted together (the largest, in green, is 140,000: 70,000 is still too big, so shift 2), so luma 17,500, green 35,000, and the pixel count is kept", h.fine[0][3] == 17500u && h.fine[2][200] == 35000u && h.n == 213539u && h.is_fine == 1u);
              chk("histogram: a bin that has pixels keeps at least 1 after the shift (red 1 and blue 3 become 1), 65,535 becomes 16,383", h.fine[1][5] == 1u && h.fine[3][9] == 1u && h.fine[3][10] == 16383u && h.fine[0][4] == 0u);
              memset(c, 0, sizeof c); c[0][0] = 65535u; c[3][255] = 7u; hs_from_counts(&h, c, 65542u);
              chk("histogram: counts that fit in 16 bits are not changed (no shift at 65,535)", h.fine[0][0] == 65535u && h.fine[3][255] == 7u && h.n == 65542u);
              memset(c, 0, sizeof c); c[0][1] = 65536u; hs_from_counts(&h, c, 65536u);
              chk("histogram: 65,536 in the luma histogram alone already shifts (the largest of ALL four histograms counts)", h.fine[0][1] == 32768u);
              /* what the page draws: a fine histogram is one bar a level, a thumbnail's is 32 bins of 8 levels; hs_clear drops the fine flag */
              memset(c, 0, sizeof c); c[1][255] = 9u; c[1][0] = 4u; c[2][77] = 11u; hs_from_counts(&h, c, 24u);
              chk("histogram page: a fine histogram gives each of the 256 levels its own bar, the tallest of the colour is the plot's maximum", h.is_fine == 1u && hs_bar(&h, 1u, 255u) == 9u && hs_bar(&h, 1u, 254u) == 0u && hs_bar(&h, 1u, 0u) == 4u && hs_plot_max(&h, 1u) == 9u && hs_plot_max(&h, 2u) == 11u && hs_plot_max(&h, 0u) == 0u);
              hs_clear(&h); hs_add(&h, 0xFFFFu); hs_add(&h, 0xF800u);
              chk("histogram page: a thumbnail's histogram is not fine, its bin i covers levels 8 i to 8 i + 7 (white: level 255 is bin 31), and the fine bars are empty after hs_clear", h.is_fine == 0u && hs_bar(&h, 1u, 255u) == 2u && hs_bar(&h, 1u, 248u) == 2u && hs_bar(&h, 1u, 247u) == 0u && hs_bar(&h, 0u, 0u) == 0u && hs_plot_max(&h, 1u) == 2u && h.fine[1][255] == 0u);
              hs_from_counts(&h, c, 24u); hs_clear(&h);
              chk("histogram page: hs_clear empties the fine bins and the flag", h.is_fine == 0u && h.fine[1][255] == 0u && h.fine[2][77] == 0u); }
            hs_clear(&h); hs_add(&h, 0xF800u); chk("histogram: pure red is bright in red only: red bin 31, green bin 0, blue bin 0, a luma bin between 8 and 10 (0.30 of 255)", h.bin[1][31] == 1u && h.bin[2][0] == 1u && h.bin[3][0] == 1u && (h.bin[0][9] == 1u || h.bin[0][8] == 1u));
            hs_clear(&h); hs_add(&h, 0x07E0u); chk("histogram: pure green has the largest luma weight: luma bin 18 or 19 (0.59 of 255)", h.bin[0][18] == 1u || h.bin[0][19] == 1u);
            hs_clear(&h); hs_add(&h, 0x001Fu); chk("histogram: pure blue has the smallest luma weight: luma bin 3 or 4 (0.11 of 255)", h.bin[0][3] == 1u || h.bin[0][4] == 1u);
            { /* the three height scales and the stats, each against a separate statement of the rule */
              int oks = 1;
              for (uint32_t v = 1u; v < 70000u; v += 13u) { const uint32_t r = hs_log2q(v); const double L = 256.0 * log2((double)v); if (fabs((double)r - L) > 24.0) oks = 0; }
              chk("scales: hs_log2q is 256 x log2 within 0.1 (every 13th value 1..70,000)", oks);
              chk("scales: LINEAR is count / max of the plot (min 1 pixel), SQRT is what it was, LOG puts a bin of 1 in 65,535 at 2 to 3 pixels of 44 (log2 2 / log2 65,536) and 1 in 100 at 25, above the SQRT bar, the tallest at the full height",
                  hs_height_m(100u, 400u, 40u, HM_LINEAR) == 10u && hs_height_m(1u, 65535u, 44u, HM_LINEAR) == 1u && hs_height_m(100u, 400u, 50u, HM_SQRT) == 25u && hs_height_m(65535u, 65535u, 44u, HM_LOG) == 44u && hs_height_m(0u, 9u, 44u, HM_LOG) == 0u
                  && hs_height_m(1u, 65535u, 44u, HM_LOG) >= 2u && hs_height_m(1u, 65535u, 44u, HM_LOG) <= 3u && hs_height_m(655u, 65535u, 44u, HM_LOG) >= 24u && hs_height_m(655u, 65535u, 44u, HM_LOG) <= 26u && hs_height_m(655u, 65535u, 44u, HM_LOG) > hs_height_m(655u, 65535u, 44u, HM_SQRT));
              hs_t h; hs_clear(&h); static uint32_t c[4][256]; memset(c, 0, sizeof c); c[0][0] = 100u; c[0][255] = 50u; c[0][100] = 850u; hs_from_counts(&h, c, 1000u); hs_stats_t st; hs_stats(&h, &st);
              chk("stats: 1,000 pixels, 100 black, 50 white, 850 at level 100: mean 100 x 0.85 + 255 x 0.05 = 97, black 10.0 %, white 5.0 %", st.exact == 1 && st.mean >= 96u && st.mean <= 98u && st.black_pm == 100u && st.white_pm == 50u);
              hs_clear(&h); hs_add(&h, 0xFFFFu); hs_add(&h, 0x0000u); hs_stats(&h, &st); chk("stats: a thumbnail's histogram gives a mean but no black and white shares", st.exact == 0 && st.black_pm == 0u && st.white_pm == 0u && st.mean > 100u && st.mean < 160u); }
            int okh = 1; for (uint32_t max = 1u; max <= 7000u; max += 37u) { uint32_t last = 0u; for (uint32_t c = 0u; c <= max; c += (max / 60u) + 1u) { const uint32_t v = hs_height(c, max, 50u); if (v < last || v > 50u || (c > 0u && v < 1u) || (c == 0u && v != 0u)) okh = 0; last = v; } if (hs_height(max, max, 50u) != 50u || hs_height(1u, max, 50u) < 1u) okh = 0; }
            chk("histogram: a bar is 0 for an empty bin, at least 1 pixel for any other, never more than the plot, grows with the count, and the tallest bin fills the plot", okh);
            chk("histogram: the height goes with the square root: a quarter of the tallest count is half the plot, a hundredth is a tenth, and 6,912 of 6,912 and 65,535 of 65,535 do not overflow", hs_height(100u, 400u, 50u) == 25u && hs_height(1728u, 6912u, 50u) == 25u && hs_height(6912u, 6912u, 50u) == 50u && hs_height(65535u, 65535u, 44u) == 44u && hs_height(655u, 65535u, 44u) >= 4u && hs_height(655u, 65535u, 44u) <= 5u && hs_height(1u, 65535u, 44u) == 1u);
            { int oks = 1; for (uint32_t v = 0; v < 65536u; v += 7u) { uint32_t r = hs_isqrt(v); if (r * r > v || (r + 1u) * (r + 1u) <= v) oks = 0; } for (uint32_t v = 0; v < 70000u; v++) { const uint32_t q = hs_isqrt(v << 8); if (q * q > (v << 8) || (q + 1u) * (q + 1u) <= (v << 8)) oks = 0; } chk("histogram: the integer square root is the floor of the real one (v and v x 256)", oks); }
            hs_clear(&h); for (uint32_t i = 0; i < 6912u; i++) hs_add(&h, 0x1234u); chk("histogram: a whole thumbnail of one colour (6,912 pixels) counts in 16 bits without wrapping", h.bin[3][0x1234u & 31u] == 6912u && hs_max(&h, 3u) == 6912u && hs_max(&h, 0u) == 6912u);
            { /* the picture's rectangle inside the thumbnail, against the converter's own rule in floating point (round half to even): for every size up to 800 x 800 and 200,000 random sizes up to 65,535 it lies INSIDE the converter's picture
               * (never a bar pixel) and at most 2 pixels in from each edge of it (one for the inset, one for a tie that the converter's floating point rounds the other way: 25 sizes of 640,000 differ by that one pixel) */
                int okr = 1, tight = 1; uint32_t seed = 7u, cases = 0u;
                for (uint32_t n = 0; n < 640000u + 200000u; n++) {
                    uint32_t w, hh;
                    if (n < 640000u) { w = 1u + n / 800u; hh = 1u + n % 800u; } else { seed = seed * 1664525u + 1013904223u; w = 1u + (seed >> 8) % 65535u; seed = seed * 1664525u + 1013904223u; hh = 1u + (seed >> 8) % 65535u; }
                    cases++;
                    double sc = fmin(fmin(96.0 / w, 72.0 / hh), 1.0); uint32_t rw = (uint32_t)nearbyint(w * sc), rh = (uint32_t)nearbyint(hh * sc); if (rw < 1u) rw = 1u; if (rh < 1u) rh = 1u; if (rw > 96u) rw = 96u; if (rh > 72u) rh = 72u;
                    const uint32_t cx0 = (96u - rw) / 2u, cy0 = (72u - rh) / 2u, cx1 = cx0 + rw, cy1 = cy0 + rh;
                    const hs_rect_t r = hs_rect(w, hh);
                    if (r.x0 < cx0 || r.y0 < cy0 || r.x1 > cx1 || r.y1 > cy1 || r.x0 >= r.x1 + (rw <= 2u) || r.y0 >= r.y1 + (rh <= 2u)) okr = 0;
                    if (r.x0 > cx0 + 2u || r.y0 > cy0 + 2u || r.x1 + 2u < cx1 || r.y1 + 2u < cy1) tight = 0;
                }
                chk("histogram rectangle: for 840,000 sizes it lies inside the converter's picture, never on a bar", okr && cases == 840000u);
                chk("histogram rectangle: and no more than 2 pixels in from each edge of it", tight);
                { const hs_rect_t a = hs_rect(192u, 101u), b = hs_rect(192u, 103u); chk("histogram rectangle: exact ties round to even as the converter does (192 x 101: 50.5 rows gives 50; 192 x 103: 51.5 gives 52)", a.y0 == 12u && a.y1 == 60u && b.y0 == 11u && b.y1 == 61u); }
                const hs_rect_t sq = hs_rect(100u, 100u), land = hs_rect(3506u, 2329u), por = hs_rect(8192u, 12288u), tiny = hs_rect(40u, 30u), unk = hs_rect(0u, 500u), huge = hs_rect(70000u, 100u);
                chk("histogram rectangle: a square picture leaves bars left and right (72 wide), a 3:2 landscape bars above and below, a portrait bars at the sides; a tiny picture is its own size; no size or over 65,535 counts the whole thumbnail",
                    sq.y0 == 0u && sq.y1 == 72u && sq.x0 == 13u && sq.x1 == 83u && land.x0 == 0u && land.x1 == 96u && land.y0 > 4u && land.y1 < 68u && por.y0 == 0u && por.y1 == 72u && por.x0 > 24u && por.x1 < 72u
                    && tiny.x1 - tiny.x0 == 38u && tiny.y1 - tiny.y0 == 28u && unk.x0 == 0u && unk.x1 == 96u && unk.y1 == 72u && huge.x1 == 96u && huge.y1 == 72u);
                chk("histogram rectangle: pixels inside and outside", hs_inside(&land, 36u, 48u) && !hs_inside(&land, 0u, 48u) && !hs_inside(&land, 71u, 48u) && !hs_inside(&por, 36u, 0u) && !hs_inside(&por, 36u, 95u) && hs_inside(&por, 36u, 48u));
            }
            chk("histogram: the source note fits under the frames (19 characters)", strlen(GD_HIST_SRC) <= 24u);
        }
        { uint32_t found = 0; for (uint32_t i = 0; i < GC_N; i++) if (!strcmp(gc_rows[i].key, "SELECT") && !strcmp(gc_rows[i].what, "PICTURE DETAILS")) found++; chk("controls: SELECT is listed for the grid and for the picture view", found == 2u); }
    }
    int ok = 1;
    for (uint32_t p = 0; p < GH_PAGES; p++) {
        if (gh_pages[p].n > 17u) ok = 0;
        for (uint32_t i = 0; i < gh_pages[p].n; i++) if (strlen(gh_pages[p].lines[i].s) > GP_MAXCH) { ok = 0; printf("  too long: %s\n", gh_pages[p].lines[i].s); }
    }
    chk("every help line fits (36 characters) and a page fits the screen (17 lines)", ok);
    ok = 1;
    for (uint32_t i = 0; i < GC_N; i++) if (strlen(gc_rows[i].key) > 21u || strlen(gc_rows[i].what) > 25u) { ok = 0; printf("  too long: %s / %s\n", gc_rows[i].key, gc_rows[i].what); }
    chk("every controls key (21) and meaning (25) fits its column", ok);
    chk("the menu names fit", strlen(gm_names[0]) < 16 && strlen(gm_names[1]) < 16 && strlen(gm_names[2]) < 16 && strlen(gm_names[3]) < 16 && strlen(gm_names[4]) < 16);
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
