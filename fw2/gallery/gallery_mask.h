/* fw2/gallery/gallery_mask.h -- Tau Gallery 1.0: crop masks on the picture view (owner request 2026-11-13: "a set of masks to show different crops on the first, fit image view"). Pure C99, no hardware, host-tested
 * (sim/test_gallery_mask.py). L1 cycles the masks, Start turns the current one (landscape to portrait and back, not the square). A mask is the largest rectangle of its aspect ratio that fits in the picture's rectangle
 * on the screen, centred; everything outside it is painted black on the picture's plane (the view's own pixels), so the picture can be judged at that crop. The edges in x are even pixels so every black copy starts and ends on a
 * halfword of the RGB888 plane (3 bytes a pixel): the pixels of the crop are never touched. */
#ifndef GALLERY_MASK_H
#define GALLERY_MASK_H
#include <stdint.h>
#define GM_N 8u                                                          /* the number of settings, 0 = off */
typedef struct { uint16_t n, d; const char *name; } gm_ratio_t;           /* the landscape form n : d (width : height) */
static const gm_ratio_t gm_ratio[GM_N] = { { 0, 0, "OFF" }, { 1, 1, "1:1" }, { 5, 4, "4:5" }, { 3, 2, "3:2" }, { 16, 9, "16:9" }, { 239, 100, "2.39:1 CINEMASCOPE" }, { 65, 24, "65:24 XPAN" }, { 3, 1, "3:1" } };
static inline uint32_t gm_next(uint32_t i) { return (i + 1u) % GM_N; }
static inline int gm_can_turn(uint32_t i) { return i > 1u && i < GM_N; }    /* every mask but the square can be turned */
/* The crop of mask i (1..GM_N-1) inside the picture rectangle (x, y, w, h) of the screen's plane; portrait != 0 turns the ratio (not for the square). Outputs the crop's edges [x0, x1) x [y0, y1); x0 and x1 are even. */
static inline void gm_crop(uint32_t i, uint32_t portrait, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1)
{
    uint32_t n = gm_ratio[i].n, d = gm_ratio[i].d;
    if (portrait && gm_can_turn(i)) { const uint32_t t = n; n = d; d = t; }
    uint32_t cw, ch;                                                      /* the largest n : d rectangle in w x h */
    if ((uint64_t)w * d >= (uint64_t)h * n) { ch = h; cw = (uint32_t)((uint64_t)h * n / d); }
    else { cw = w; ch = (uint32_t)((uint64_t)w * d / n); }
    if (cw < 2u) cw = 2u;
    if (ch < 1u) ch = 1u;
    uint32_t cx = x + (w - cw) / 2u, cy = y + (h - ch) / 2u;
    cx &= ~1u;                                                            /* even */
    uint32_t ex = (cx + cw + 1u) & ~1u;                                   /* the right edge, even, rounded outwards */
    *x0 = cx; *x1 = ex; *y0 = cy; *y1 = cy + ch;
}
#endif
