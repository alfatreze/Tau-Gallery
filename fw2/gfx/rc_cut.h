/* The corner-cut table of the draw engine (RTL: rc_cut_lut, 16 entries of 5 bits, written through R_RC_IDX / R_RC_DATA): entry dy (the row's distance from the corner's tangent row, dy = r - row, 1..r)
 * is how many pixels the corner takes off that row. The table resets to ZERO in the core, so the firmware has to load it before the first OP_RRECT or OP_CORNERS (without it every rounded corner of the
 * hardware ops is square: the grid's rounded thumbnails and selection frame were, on DEV 023 and 024). Pure integer maths, host-tested against a floating point circle (sim/p2_gfx_harness.c).
 * The shape: the pixel row's centre is at distance dy - 1/2 from the corner circle's centre line, the circle runs at x = r - sqrt(r^2 - (dy - 1/2)^2), rounded to the nearest pixel. */
#ifndef RC_CUT_H
#define RC_CUT_H
#include <stdint.h>
static inline uint32_t gfx_rc_cut(uint32_t r, uint32_t dy)
{
    if (!r || dy < 1u || dy > r) return 0u;
    const uint32_t y = 4u * (r * r - dy * dy + dy) - 1u;            /* 4 * (r^2 - (dy - 1/2)^2) */
    uint32_t k = 0u;
    while ((2u * k + 1u) * (2u * k + 1u) <= y) k++;                 /* k = round(sqrt(y / 4)) */
    return r > k ? r - k : 0u;
}
#endif
