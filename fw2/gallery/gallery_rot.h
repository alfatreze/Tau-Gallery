/* fw2/gallery/gallery_rot.h -- Tau Gallery 1.0: picture orientation (rotation and mirror) for the hardware decoder's scatter writer (docs/features/GALLERY_RTL_1_0_SPEC.md, K7). Pure C99, no hardware, host-tested
 * (sim/test_gallery_rot.py). An orientation o (0..7) maps a source pixel (x, y) of the picture to its place (X, Y) in the displayed picture:
 *   o: 0 as it is   1 mirror left-right   2 mirror top-bottom   3 rotated 180   4 transposed   5 rotated 90 counter-clockwise   6 rotated 90 clockwise   7 anti-transposed
 *   (X, Y) = A * (x, y) + c with A = {{a00, a01}, {a10, a11}}: 0 {{1,0},{0,1}}  1 {{-1,0},{0,1}}  2 {{1,0},{0,-1}}  3 {{-1,0},{0,-1}}  4 {{0,1},{1,0}}  5 {{0,1},{-1,0}}  6 {{0,-1},{1,0}}  7 {{0,-1},{-1,0}}
 * c puts the picture's corner back at the origin. A transposing orientation (4..7) shows the picture with its width and height exchanged. The scatter writer takes the byte address of the tile's source pixel (0, 0) and two
 * signed byte steps: x + 1 moves by stx, y + 1 by sty; with the plane's pitch P bytes and 3 bytes a pixel, stx = a00 * 3 + a10 * P and sty = a01 * 3 + a11 * P. */
#ifndef GALLERY_ROT_H
#define GALLERY_ROT_H
#include <stdint.h>
#define GR_ID 0u
#define GR_NO 8u                                                      /* the number of orientations */
static const int8_t gr_a[8][4] = { { 1, 0, 0, 1 }, { -1, 0, 0, 1 }, { 1, 0, 0, -1 }, { -1, 0, 0, -1 }, { 0, 1, 1, 0 }, { 0, 1, -1, 0 }, { 0, -1, 1, 0 }, { 0, -1, -1, 0 } };
static inline int gr_swaps(uint32_t o) { return o >= 4u; }
static inline void gr_dims(uint32_t o, uint32_t iw, uint32_t ih, uint32_t *dw, uint32_t *dh) { *dw = gr_swaps(o) ? ih : iw; *dh = gr_swaps(o) ? iw : ih; }
/* The place of source pixel (x, y) of an iw x ih picture in the displayed picture. */
static inline void gr_map(uint32_t o, uint32_t iw, uint32_t ih, uint32_t x, uint32_t y, uint32_t *X, uint32_t *Y)
{
    const int32_t sx = (int32_t)x, sy = (int32_t)y, w1 = (int32_t)iw - 1, h1 = (int32_t)ih - 1;
    int32_t px = gr_a[o][0] * sx + gr_a[o][1] * sy, py = gr_a[o][2] * sx + gr_a[o][3] * sy;
    /* c: the corner of the picture that A sends to a negative coordinate comes back to zero */
    int32_t cx = 0, cy = 0;
    if (gr_a[o][0] < 0) cx += w1; if (gr_a[o][1] < 0) cx += h1;
    if (gr_a[o][2] < 0) cy += w1; if (gr_a[o][3] < 0) cy += h1;
    *X = (uint32_t)(px + cx); *Y = (uint32_t)(py + cy);
}
static inline int32_t gr_stx(uint32_t o, int32_t pitch) { return gr_a[o][0] * 3 + gr_a[o][2] * pitch; }
static inline int32_t gr_sty(uint32_t o, int32_t pitch) { return gr_a[o][1] * 3 + gr_a[o][3] * pitch; }
/* The byte offset, in a displayed picture of pitch P whose pixel (0, 0) is at offset 0, of the tile whose source pixel (0, 0) is the picture's pixel (sx, sy). */
static inline uint32_t gr_tile_off(uint32_t o, uint32_t iw, uint32_t ih, uint32_t sx, uint32_t sy, uint32_t pitch)
{
    uint32_t X, Y; gr_map(o, iw, ih, sx, sy, &X, &Y); return Y * pitch + X * 3u;
}
/* The orientation after turning the picture 90 degrees clockwise (counter-clockwise) from o: the matrix of the turn times the matrix of o. */
static inline uint32_t gr_find(int32_t a00, int32_t a01, int32_t a10, int32_t a11)
{
    for (uint32_t o = 0; o < 8u; o++) if (gr_a[o][0] == a00 && gr_a[o][1] == a01 && gr_a[o][2] == a10 && gr_a[o][3] == a11) return o;
    return 0u;
}
static inline uint32_t gr_turn(uint32_t o, int cw)
{
    const int32_t r00 = 0, r01 = cw ? -1 : 1, r10 = cw ? 1 : -1, r11 = 0;              /* the turn: clockwise {{0,-1},{1,0}}, counter-clockwise {{0,1},{-1,0}} */
    return gr_find(r00 * gr_a[o][0] + r01 * gr_a[o][2], r00 * gr_a[o][1] + r01 * gr_a[o][3], r10 * gr_a[o][0] + r11 * gr_a[o][2], r10 * gr_a[o][1] + r11 * gr_a[o][3]);
}
static inline uint32_t gr_mirror(uint32_t o) { return gr_find(-gr_a[o][0], -gr_a[o][1], gr_a[o][2], gr_a[o][3]); }      /* mirrored left-right on the screen */
static inline uint32_t gr_flip(uint32_t o) { return gr_find(gr_a[o][0], gr_a[o][1], -gr_a[o][2], -gr_a[o][3]); }                 /* mirrored top-bottom on the screen */
#endif
