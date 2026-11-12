/* Host test of fw2/gallery/gallery_pages.h (menu, Controls list, Help pages). Compile with -DBUG=n for the mutants (1..4 must fail). */
#include <stdio.h>
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
        chk("info: B and Left close, others do not", gi_input(GP_KEY_B) && gi_input(GP_KEY_LEFT) && !gi_input(GP_KEY_UP) && !gi_input(GP_KEY_A));
    }
    {
        uint32_t rs = 1u;
        chk("settings: A turns resume off", gsp_input(&rs, GP_KEY_A) == 0 && rs == 0u);
        chk("settings: Left turns it on", gsp_input(&rs, GP_KEY_LEFT) == 0 && rs == 1u);
        chk("settings: Right turns it off", gsp_input(&rs, 1u << 3) == 0 && rs == 0u);
        rs = 5u; gsp_input(&rs, GP_KEY_A); chk("settings: the flag stays 0 or 1 whatever it held", rs == 0u);
        chk("settings: B closes and changes nothing", gsp_input(&rs, GP_KEY_B) == 1 && rs == 0u);
        chk("settings: Up, Down and Start neither change nor close", gsp_input(&rs, GP_KEY_UP) == 0 && gsp_input(&rs, GP_KEY_DOWN) == 0 && gsp_input(&rs, GP_KEY_START) == 0 && rs == 0u);
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
        chk("details: the rows that need unread data say so", !strcmp(r[4].value, GD_NOT_READ) && !strcmp(r[5].value, GD_NOT_READ) && !strcmp(r[6].value, GD_NOT_READ) && !strcmp(r[7].value, GD_NOT_READ)
            && !strcmp(r[4].label, "FORMAT") && !strcmp(r[5].label, "CAMERA") && !strcmp(r[6].label, "TAKEN") && !strcmp(r[7].label, "EXPOSURE"));
        char b[28]; gd_size(b, 0u) [0] = 0; chk("details: 0 bytes", !strcmp(b, "0 B")); *gd_size(b, 1023u) = 0; chk("details: 1023 bytes stays in bytes", !strcmp(b, "1023 B"));
        *gd_size(b, 1024u) = 0; chk("details: 1024 bytes is 1 KB", !strcmp(b, "1 KB")); *gd_size(b, 1048575u) = 0; chk("details: just under 1 MiB is 1023 KB", !strcmp(b, "1023 KB"));
        *gd_size(b, 1048576u) = 0; chk("details: 1 MiB is 1.0 MB", !strcmp(b, "1.0 MB")); *gd_size(b, 4294967295u) = 0; chk("details: the largest size does not overflow (4095.9 MB)", !strcmp(b, "4095.9 MB"));
        d.width = 0u; gd_format(&d, r); chk("details: a picture whose index has no size says UNKNOWN", !strcmp(r[2].value, "UNKNOWN"));
        d.width = 800u; d.height = 0u; gd_format(&d, r); chk("details: so does a missing height", !strcmp(r[2].value, "UNKNOWN"));
        gd_t longn = { "0123456789012345678901234567890123456789", 0u, 1u, 10u, 1u, 1u }; gd_format(&longn, r);
        chk("details: a long name is cut to the column (26) and ends", strlen(r[0].value) == 26u && !strncmp(r[0].value, "01234567890123456789012345", 26));
        { uint32_t okw = 1; gd_t w = { "N", 4294967294u, 4294967295u, 4294967295u, 4294967295u, 4294967295u }; gd_format(&w, r); for (uint32_t i = 0; i < GD_ROWS; i++) if (strlen(r[i].value) > 27u) okw = 0; chk("details: the widest values fit the 28 byte value field", okw); }
        chk("details: B closes", gd_input(GP_KEY_B) == 1); chk("details: Select closes (it opens the page, so it toggles)", gd_input(GP_KEY_SELECT) == 1);
        chk("details: A, Up, Down, Left, Right and Start do not close", gd_input(GP_KEY_A) == 0 && gd_input(GP_KEY_UP) == 0 && gd_input(GP_KEY_DOWN) == 0 && gd_input(GP_KEY_LEFT) == 0 && gd_input(1u << 3) == 0 && gd_input(GP_KEY_START) == 0);
        chk("details: four histogram frames, in the order luma, red, green, blue", GD_HIST_N == 4u && !strcmp(gd_hist_labels[0], "LUMA") && !strcmp(gd_hist_labels[1], "RED") && !strcmp(gd_hist_labels[2], "GREEN") && !strcmp(gd_hist_labels[3], "BLUE"));
        chk("details: the histogram note fits a frame (16 characters)", strlen(GD_HIST_NOTE) <= 16u);
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
