/* Host test of fw2/gallery/gallery_pattern.c: the test patterns of the Tau Gallery M0 page and the frozen plane format (docs/M0_DESIGN.md 3). Prints "GALLERY PATTERN ok n" per check; exit 1 on any failure. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../fw2/gallery/gallery_pattern.h"
static int fails;
static void chk(int n, int ok) { printf("GALLERY PATTERN %s %d\n", ok ? "ok" : "FAIL", n); if (!ok) fails++; }
static uint8_t px(uint32_t pat, uint32_t x, uint32_t y, int c) { uint8_t t[3]; gal_pixel(pat, x, y, t); return t[c]; }
int main(void)
{
    chk(1, GAL_LINE_BYTES == 2400u && GAL_LINE_WORDS16 == 1200u && GAL_LINE_WORDS32 == 600u && GAL_LINE_WORDS16 * GAL_H == 864000u);
    /* ramp: bands, endpoints, monotone */
    int mono = 1; for (uint32_t b = 0; b < 4; b++) for (uint32_t x = 2; x < GAL_W - 1; x++) { const uint32_t y = b * 180u + 90u; if (px(GP_RAMP, x, y, b ? (int)b - 1 : 0) < px(GP_RAMP, x - 1, y, b ? (int)b - 1 : 0)) mono = 0; }
    chk(2, mono);
    chk(3, px(GP_RAMP, 1, 90, 0) <= 1 && px(GP_RAMP, 798, 90, 0) >= 254);
    chk(4, px(GP_RAMP, 400, 270, 1) == 0 && px(GP_RAMP, 400, 270, 2) == 0 && px(GP_RAMP, 400, 270, 0) > 100);                 /* red band: only red */
    chk(5, px(GP_RAMP, 400, 450, 0) == 0 && px(GP_RAMP, 400, 450, 2) == 0 && px(GP_RAMP, 400, 450, 1) > 100);                /* green band */
    chk(6, px(GP_RAMP, 400, 630, 0) == 0 && px(GP_RAMP, 400, 630, 1) == 0 && px(GP_RAMP, 400, 630, 2) > 100);                /* blue band */
    /* frame: the four edges are white in every pattern but the texture */
    int frame = 1; for (uint32_t p = 0; p < GP_TEXTURE; p++) for (int c = 0; c < 3; c++) {
        if (px(p, 0, 300, c) != 255 || px(p, 799, 300, c) != 255 || px(p, 300, 0, c) != 255 || px(p, 300, 719, c) != 255) frame = 0; }
    chk(7, frame);
    /* gradient */
    chk(8, px(GP_GRADIENT, 1, 1, 0) < 3 && px(GP_GRADIENT, 798, 1, 0) > 252 && px(GP_GRADIENT, 1, 718, 1) > 252 && px(GP_GRADIENT, 1, 1, 1) < 3);
    chk(9, px(GP_GRADIENT, 400, 360, 2) == 255 - ((px(GP_GRADIENT, 400, 360, 0) + px(GP_GRADIENT, 400, 360, 1)) >> 1));
    /* checker: only 0 and 255, 8 pixel squares alternate in both directions */
    int only = 1, alt = 1; for (uint32_t y = 1; y < 100; y++) for (uint32_t x = 1; x < 100; x++) {
        uint8_t v = px(GP_CHECKER, x, y, 0); if (v != 0 && v != 255) only = 0;
        if (((x & 7u) != 7u) && px(GP_CHECKER, x + 1, y, 0) != v) alt = 0;                      /* inside a square nothing changes */
        if (((x & 7u) == 7u) && x + 1 < 800 && px(GP_CHECKER, x + 1, y, 0) == v) alt = 0; }     /* across the square edge it flips */
    chk(10, only && alt);
    /* texture: no frame, many distinct values in every channel, not constant along a line */
    int seen[3][256]; memset(seen, 0, sizeof seen); for (uint32_t x = 0; x < 800; x++) for (int c = 0; c < 3; c++) seen[c][px(GP_TEXTURE, x, 123, c)] = 1;
    int distinct = 1; for (int c = 0; c < 3; c++) { int n = 0; for (int v = 0; v < 256; v++) n += seen[c][v]; if (n < 100) distinct = 0; }
    chk(11, distinct && !(px(GP_TEXTURE, 0, 300, 0) == 255 && px(GP_TEXTURE, 0, 300, 1) == 255 && px(GP_TEXTURE, 0, 300, 2) == 255));
    /* a line is the pixels in R, G, B order, and the packing is little-endian: word i = {byte(2i+1), byte(2i)} */
    static uint8_t line[GAL_LINE_BYTES]; gal_line(GP_TEXTURE, 77, line);
    int order = 1; for (uint32_t x = 0; x < 800; x += 37) { uint8_t t[3]; gal_pixel(GP_TEXTURE, x, 77, t); if (line[3 * x] != t[0] || line[3 * x + 1] != t[1] || line[3 * x + 2] != t[2]) order = 0; }
    chk(12, order);
    int pack = 1; for (uint32_t i = 0; i < GAL_LINE_WORDS32; i++) {
        const uint32_t w = gal_pack32(line, i), lo = w & 0xFFFFu, hi = w >> 16;                  /* plane word 2i is the low half, 2i+1 the high half */
        if (lo != ((uint32_t)line[4 * i + 1] << 8 | line[4 * i]) || hi != ((uint32_t)line[4 * i + 3] << 8 | line[4 * i + 2])) pack = 0; }
    chk(13, pack);
    /* the three bytes of the first pixel sit in the low three bytes of word 0 */
    chk(14, (gal_pack32(line, 0) & 0xFFFFFFu) == ((uint32_t)line[2] << 16 | (uint32_t)line[1] << 8 | line[0]));
    /* four pixels are three words: the fourth pixel's bytes are the top three bytes of word 2 */
    chk(15, (gal_pack32(line, 2) >> 8) == ((uint32_t)line[11] << 16 | (uint32_t)line[10] << 8 | line[9]));
    /* every pattern is deterministic and differs from the others at some pixel */
    int diff = 1; for (uint32_t a = 0; a < GP_COUNT; a++) for (uint32_t b = a + 1; b < GP_COUNT; b++) { int d = 0;
        for (uint32_t y = 40; y < 700 && !d; y += 97) for (uint32_t x = 40; x < 780 && !d; x += 89) for (int c = 0; c < 3; c++) if (px(a, x, y, c) != px(b, x, y, c)) d = 1; if (!d) diff = 0; }
    chk(16, diff);
    printf(fails ? "GALLERY PATTERN FAILED %d\n" : "GALLERY PATTERN PASSED (0 failures)\n", fails);
    return fails ? 1 : 0;
}
