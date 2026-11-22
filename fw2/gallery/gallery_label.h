/* fw2/gallery/gallery_label.h -- Tau Gallery 1.0: the name of what a key just changed (the colour filter, the crop mask, the turn), shown as a black bar with white text at the bottom of the viewer's plane (owner request
 * 2026-11-19: "when swapping filter, show the name ... maybe just a black bar on the bottom with the name"). The viewer plane has no overlay, so the bar is written into the plane itself and the rows under it are copied away
 * first and back afterwards (the way the zoom's loader bar works). Pure C99, no hardware, host-tested (sim/test_gallery_label.py). The font is the classic 5 x 7 one, drawn 3 times as large (15 x 21 pixels, 18 pixels a
 * character with the gap), capitals, digits and  : . / - +  only. The text starts 16 pixels in from the left edge and 5 rows down in the bar. */
#ifndef GALLERY_LABEL_H
#define GALLERY_LABEL_H
#include <stdint.h>
#define GLB_Y0 688u                                  /* the first plane row of the bar (a plane shows 720 rows: the bar is the last 32) */
#define GLB_H 32u
#define GLB_TX 16u                                   /* the text's left edge, and its first row inside the bar */
#define GLB_TY 5u
#define GLB_SCALE 3u
#define GLB_ADV 18u                                  /* 5 columns and a gap, each 3 pixels */
#define GLB_TH 21u                                   /* 7 rows of 3 */
#define GLB_PX 560u                                  /* the pixels of a bar row that carry text (the rest of the row is plain black): 560 x 3 bytes is a whole number of words */
#define GLB_MAXCH 30u                                /* (560 - 16) / 18 */
typedef struct { char c; uint8_t col[5]; } glb_glyph_t;                /* five columns, bit 0 the top row */
static const glb_glyph_t glb_font[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } }, { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } }, { '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } }, { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } }, { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } }, { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } }, { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } }, { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } }, { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } }, { '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } }, { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } }, { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }, { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } }, { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } }, { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } }, { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } }, { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } }, { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } }, { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } }, { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } }, { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } }, { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } }, { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } }, { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } }, { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } }, { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } }, { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } }, { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } }, { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } }, { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } }, { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
};
#define GLB_NGLYPH ((uint32_t)(sizeof glb_font / sizeof glb_font[0]))
static inline const uint8_t *glb_cols(char c)                          /* the five columns of c (a blank for a character the font has not) */
{
    static const uint8_t blank[5] = { 0, 0, 0, 0, 0 };
    for (uint32_t i = 0; i < GLB_NGLYPH; i++) if (glb_font[i].c == c) return glb_font[i].col;
    return blank;
}
static inline uint32_t glb_len(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
/* Is the pixel (x, y) of the text (x from the text's left edge, y from its top) white, the text drawn sc times the font's size (3 in the full-size plane, 1 on a plane that scan-out doubles)? */
static inline int glb_on_g(const char *s, uint32_t x, uint32_t y, uint32_t sc)
{
    const uint32_t ci = x / (6u * sc), within = (x % (6u * sc)) / sc, row = y / sc;
    if (ci >= glb_len(s) || ci >= GLB_MAXCH || within >= 5u || row >= 7u) return 0;
    return (glb_cols(s[ci])[within] >> row) & 1u;
}
static inline int glb_on(const char *s, uint32_t x, uint32_t y) { return glb_on_g(s, x, y, GLB_SCALE); }
/* One row of the text (ty 0..20) as the RGB888 bytes of the bar row's first GLB_PX pixels: white where the text is, black elsewhere. rb holds GLB_PX x 3 bytes. */
static inline void glb_row(const char *s, uint32_t ty, uint8_t *rb)
{
    for (uint32_t x = 0; x < GLB_PX; x++) {
        const uint8_t v = (x >= GLB_TX && glb_on(s, x - GLB_TX, ty)) ? 255u : 0u;
        rb[3u * x] = v; rb[3u * x + 1u] = v; rb[3u * x + 2u] = v;
    }
}
/* Word w (bytes 4 w .. 4 w + 3 of the row's RGB888 stream, the first byte lowest) of text row ty, the text at scale sc and starting tx pixels in: the same bytes glb_row writes, without a buffer. */
static inline uint32_t glb_word_g(const char *s, uint32_t ty, uint32_t w, uint32_t sc, uint32_t tx)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4u; i++) { const uint32_t x = (4u * w + i) / 3u; if (x >= tx && glb_on_g(s, x - tx, ty, sc)) v |= 255u << (8u * i); }
    return v;
}
static inline uint32_t glb_word(const char *s, uint32_t ty, uint32_t w) { return glb_word_g(s, ty, w, GLB_SCALE, GLB_TX); }
static inline char *glb_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
/* The names of the eight orientations (gallery_rot.h numbering). */
static const char *const glb_orient[8] = { "UPRIGHT", "MIRRORED", "FLIPPED", "TURNED 180", "TRANSPOSED", "TURNED LEFT", "TURNED RIGHT", "ANTI-TRANSPOSED" };
#endif
