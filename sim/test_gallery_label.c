/* Host test of fw2/gallery/gallery_label.h: the font as ASCII art written here (a separate statement of each glyph), the scaling, the row bytes, the bounds, and that every character of every name the viewer shows has a glyph. */
#include <stdio.h>
#include <string.h>
#include "../fw2/gallery/gallery_lut.h"
#include "../fw2/gallery/gallery_mask.h"
#include "../fw2/gallery/gallery_label.h"
static int fails;
static void chk(const char *n, int ok) { printf(ok ? "ok   %s\n" : "FAIL %s\n", n); if (!ok) fails++; }
typedef struct { char c; const char *art[7]; } art_t;
static const art_t ART[] = {
    { 'A', { ".###.", "#...#", "#...#", "#...#", "#####", "#...#", "#...#" } },
    { '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
    { '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { ':', { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." } },
    { '/', { ".....", "....#", "...#.", "..#..", ".#...", "#....", "....." } },
    { '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
};
int main(void)
{
    int okart = 1;
    for (unsigned a = 0; a < sizeof ART / sizeof ART[0]; a++) {
        char s[4] = { 'T', ART[a].c, 'T', 0 };                                                    /* the glyph in the middle of two others: the advance is 18 */
        for (unsigned r = 0; r < 7; r++) for (unsigned c = 0; c < 5; c++) for (unsigned dy = 0; dy < 3; dy++) for (unsigned dx = 0; dx < 3; dx++) {
            const int want = ART[a].art[r][c] == '#', got = glb_on(s, 18u + c * 3u + dx, r * 3u + dy);
            if (want != got) { okart = 0; printf("  %c row %u col %u\n", ART[a].c, r, c); }
        }
        for (unsigned y = 0; y < 21; y++) for (unsigned dx = 0; dx < 3; dx++) if (glb_on(s, 18u + 15u + dx, y)) okart = 0;                /* the gap after a glyph is empty */
    }
    chk("glyphs: nine letters, digits and signs against their ASCII art, each pixel 3 x 3, the gap column empty", okart);
    chk("bounds: nothing right of the text, nothing below the 21 rows, nothing for a space or an unknown character", !glb_on("A", 18u, 0u) && !glb_on("A", 0u, 21u) && !glb_on(" ", 3u, 9u) && !glb_on("~", 3u, 9u) && !glb_on("A", 0u, 100u));
    { uint8_t rb[GLB_PX * 3u]; glb_row("A", 3u, rb);                                                                /* the A's second font row is '#...#': columns 0 and 4 */
      int ok = 1; for (unsigned x = 0; x < GLB_PX; x++) { const int on = (x >= 16u && x < 16u + 3u) || (x >= 16u + 12u && x < 16u + 15u); for (unsigned k = 0; k < 3; k++) if (rb[3u * x + k] != (on ? 255u : 0u)) ok = 0; }
      chk("a row: white at the text's own pixels 16 in from the left (the A's left and right strokes), black elsewhere, grey nowhere", ok); }
    { char b[GLB_MAXCH + 8]; memset(b, 'W', sizeof b); b[sizeof b - 1] = 0; uint8_t rb[GLB_PX * 3u]; glb_row(b, 9u, rb);
      chk("a text longer than the bar's 30 characters is cut at 30, not drawn on past the row's text pixels", GLB_MAXCH * GLB_ADV + GLB_TX <= GLB_PX && !glb_on(b, GLB_MAXCH * GLB_ADV, 9u) && !glb_on(b, GLB_MAXCH * GLB_ADV + 20u, 9u)); }
    { const char *all[40]; unsigned n = 0; char t[64]; int ok = 1;
      for (unsigned i = 0; i < GL_PRESETS; i++) { char *q = glb_cat(t, "FILTER "); glb_cat(q, gl_names[i]); for (const char *p = t; *p; p++) if (*p != ' ' && glb_cols(*p)[0] == 0 && glb_cols(*p)[1] == 0 && glb_cols(*p)[2] == 0 && glb_cols(*p)[3] == 0 && glb_cols(*p)[4] == 0) { ok = 0; printf("  no glyph for '%c' in %s\n", *p, t); } if (glb_len(t) > GLB_MAXCH) ok = 0; }
      for (unsigned i = 0; i < GM_N; i++) { char *q = glb_cat(t, "MASK "); q = glb_cat(q, gm_ratio[i].name); glb_cat(q, " TALL"); for (const char *p = t; *p; p++) if (*p != ' ' && glb_cols(*p)[0] == 0 && glb_cols(*p)[1] == 0 && glb_cols(*p)[2] == 0 && glb_cols(*p)[3] == 0 && glb_cols(*p)[4] == 0) { ok = 0; printf("  no glyph for '%c' in %s\n", *p, t); } if (glb_len(t) > GLB_MAXCH) { ok = 0; printf("  too long: %s\n", t); } }
      for (unsigned i = 0; i < 8; i++) { for (const char *p = glb_orient[i]; *p; p++) if (*p != ' ' && glb_cols(*p)[0] == 0 && glb_cols(*p)[1] == 0 && glb_cols(*p)[2] == 0 && glb_cols(*p)[3] == 0 && glb_cols(*p)[4] == 0) { ok = 0; printf("  no glyph for '%c' in %s\n", *p, glb_orient[i]); } if (glb_len(glb_orient[i]) > GLB_MAXCH) ok = 0; }
      (void)all; (void)n;
      chk("every filter, mask (turned) and orientation name has a glyph for each character and fits the 30 characters of the bar", ok); }
    { int ok = 1; const char *t = "FILTER INVERT"; uint8_t rb[GLB_PX * 3u];
      for (unsigned ty = 0; ty < 21; ty += 3) { glb_row(t, ty, rb); for (unsigned w = 0; w < GLB_PX * 3u / 4u; w++) { const uint32_t want = (uint32_t)rb[4u * w] | (uint32_t)rb[4u * w + 1u] << 8 | (uint32_t)rb[4u * w + 2u] << 16 | (uint32_t)rb[4u * w + 3u] << 24; if (glb_word(t, ty, w) != want) ok = 0; } }
      chk("glb_word gives exactly the bytes glb_row writes, four at a time (first byte lowest), for every word of the row", ok); }
    { char b[16]; char *q = glb_cat(b, "AB"); q = glb_cat(q, "CD"); chk("glb_cat appends and returns the end", !strcmp(b, "ABCD") && q == b + 4); }
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails); return fails != 0;
}
