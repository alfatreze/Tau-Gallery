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

    int ok = 1;
    for (uint32_t p = 0; p < GH_PAGES; p++) {
        if (gh_pages[p].n > 17u) ok = 0;
        for (uint32_t i = 0; i < gh_pages[p].n; i++) if (strlen(gh_pages[p].lines[i].s) > GP_MAXCH) { ok = 0; printf("  too long: %s\n", gh_pages[p].lines[i].s); }
    }
    chk("every help line fits (36 characters) and a page fits the screen (17 lines)", ok);
    ok = 1;
    for (uint32_t i = 0; i < GC_N; i++) if (strlen(gc_rows[i].key) > 21u || strlen(gc_rows[i].what) > 25u) { ok = 0; printf("  too long: %s / %s\n", gc_rows[i].key, gc_rows[i].what); }
    chk("every controls key (21) and meaning (25) fits its column", ok);
    chk("the menu names fit", strlen(gm_names[0]) < 16 && strlen(gm_names[1]) < 16);
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
