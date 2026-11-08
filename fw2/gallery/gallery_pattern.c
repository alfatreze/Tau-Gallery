#include "gallery_pattern.h"

const char *const gal_pattern_name[GP_COUNT] = { "RAMP", "GRADIENT", "CHECKER", "TEXTURE" };

static uint32_t tri(uint32_t v) { v &= 511u; return v < 256u ? v : 511u - v; }      /* a triangle wave 0..255..0, period 512 */

void gal_pixel(uint32_t pat, uint32_t x, uint32_t y, uint8_t rgb[3])
{
    uint32_t r = 0, g = 0, b = 0;
    if (pat != GP_TEXTURE && (x == 0u || y == 0u || x == GAL_W - 1u || y == GAL_H - 1u)) { rgb[0] = rgb[1] = rgb[2] = 255u; return; }
    switch (pat) {
    case GP_RAMP: {                                         /* four bands of 180 lines: grey, red, green, blue ramps, 0 at the left edge to 255 at the right */
        const uint32_t v = (x * 255u + 399u) / 799u, band = y / 180u;
        r = band == 0u || band == 1u ? v : 0u; g = band == 0u || band == 2u ? v : 0u; b = band == 0u || band == 3u ? v : 0u;
        break; }
    case GP_GRADIENT:                                       /* red grows to the right, green downwards, blue is what is left */
        r = (x * 255u + 399u) / 799u; g = (y * 255u + 359u) / 719u; b = 255u - ((r + g) >> 1);
        break;
    case GP_CHECKER:                                        /* 8-pixel squares: every pixel is either exactly black or exactly white */
        r = g = b = (((x >> 3) ^ (y >> 3)) & 1u) ? 255u : 0u;
        break;
    default:                                                /* GP_TEXTURE: smooth diagonal waves with all three channels moving (no flat areas, every byte position busy) */
        r = tri(x + y); g = tri(2u * x + 976u - y); b = tri(3u * y / 2u + x / 2u);
        break;
    }
    rgb[0] = (uint8_t)r; rgb[1] = (uint8_t)g; rgb[2] = (uint8_t)b;
}

void gal_line(uint32_t pat, uint32_t y, uint8_t *bytes)
{
    for (uint32_t x = 0; x < GAL_W; x++) gal_pixel(pat, x, y, bytes + 3u * x);
}

uint32_t gal_pack32(const uint8_t *b, uint32_t i)
{
    b += 4u * i;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
