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
    chk("down from Settings wraps to Controls", gm_input(&m, GP_KEY_DOWN) == GP_STAY && m.sel == GM_CONTROLS);
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
    int ok = 1;
    for (uint32_t p = 0; p < GH_PAGES; p++) {
        if (gh_pages[p].n > 17u) ok = 0;
        for (uint32_t i = 0; i < gh_pages[p].n; i++) if (strlen(gh_pages[p].lines[i].s) > GP_MAXCH) { ok = 0; printf("  too long: %s\n", gh_pages[p].lines[i].s); }
    }
    chk("every help line fits (36 characters) and a page fits the screen (17 lines)", ok);
    ok = 1;
    for (uint32_t i = 0; i < GC_N; i++) if (strlen(gc_rows[i].key) > 21u || strlen(gc_rows[i].what) > 25u) { ok = 0; printf("  too long: %s / %s\n", gc_rows[i].key, gc_rows[i].what); }
    chk("every controls key (21) and meaning (25) fits its column", ok);
    chk("the menu names fit", strlen(gm_names[0]) < 16 && strlen(gm_names[1]) < 16 && strlen(gm_names[2]) < 16 && strlen(gm_names[3]) < 16);
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
