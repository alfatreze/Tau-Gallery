#include "tgy_pix.h"

static const uint8_t ZZN[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static int64_t descale(int64_t x, unsigned n) { return (x + ((int64_t)1 << (n - 1))) >> n; }       /* arithmetic shift of a negative value, as libjpeg */
static uint8_t post_idct_limit(int64_t d)                                                         /* libjpeg's post-IDCT range limit table, including the wrap beyond +-512 */
{
    unsigned j = (unsigned)(d & 1023);
    return (uint8_t)(j < 128u ? j + 128u : (j < 512u ? 255u : (j < 896u ? 0u : j - 896u)));
}
/* The 1-D butterfly of jidctint.c: i[0..7] in, o[0..7] out (before the descale). */
static void butterfly(const int64_t *i, int64_t *o)
{
    int64_t z1 = (i[2] + i[6]) * 4433, t2 = z1 + i[6] * -15137, t3 = z1 + i[2] * 6270, t0 = (i[0] + i[4]) * 8192, t1 = (i[0] - i[4]) * 8192;
    int64_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
    int64_t a0 = i[7], a1 = i[5], a2 = i[3], a3 = i[1], z2, z3, z4, z5;
    z1 = a0 + a3; z2 = a1 + a2; z3 = a0 + a2; z4 = a1 + a3; z5 = (z3 + z4) * 9633;
    a0 *= 2446; a1 *= 16819; a2 *= 25172; a3 *= 12299; z1 *= -7373; z2 *= -20995; z3 *= -16069; z4 *= -3196; z3 += z5; z4 += z5;
    a0 += z1 + z3; a1 += z2 + z4; a2 += z2 + z3; a3 += z1 + z4;
    o[0] = t10 + a3; o[1] = t11 + a2; o[2] = t12 + a1; o[3] = t13 + a0; o[4] = t13 - a0; o[5] = t12 - a1; o[6] = t11 - a2; o[7] = t10 - a3;
}
void tgy_idct_natural(const int32_t *in, uint8_t *out)
{
    int64_t ws[64], a[8], o[8]; unsigned r, c, k;
    for (c = 0; c < 8; c++) { for (r = 0; r < 8; r++) a[r] = in[r * 8 + c]; butterfly(a, o); for (r = 0; r < 8; r++) ws[r * 8 + c] = descale(o[r], 11); }
    for (r = 0; r < 8; r++) { for (k = 0; k < 8; k++) a[k] = ws[r * 8 + k]; butterfly(a, o); for (k = 0; k < 8; k++) out[r * 8 + k] = post_idct_limit(descale(o[k], 18)); }
}
void tgy_dequant_idct(const int16_t *zz, const uint16_t *qzz, uint8_t *out)
{
    int32_t nat[64]; unsigned k;
    for (k = 0; k < 64; k++) nat[ZZN[k]] = (int32_t)zz[k] * (int32_t)qzz[k];
    tgy_idct_natural(nat, out);
}
void tgy_ycc_to_rgb(unsigned y, unsigned cb, unsigned cr, uint8_t *rgb)
{
    int32_t xb = (int32_t)cb - 128, xr = (int32_t)cr - 128, r, g, b;
    r = (int32_t)y + ((91881 * xr + 32768) >> 16);
    g = (int32_t)y + ((-22554 * xb + 32768 + -46802 * xr) >> 16);
    b = (int32_t)y + ((116130 * xb + 32768) >> 16);
    rgb[0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r)); rgb[1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g)); rgb[2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
}
void tgy_upsample_row(const uint8_t *plane, unsigned cs, unsigned cw, unsigned ch, unsigned r, unsigned width, uint8_t *out)
{
    unsigned cr = r >> 1, v = r & 1u, x;
    const uint8_t *t = plane + (uint32_t)cr * cs, *o;
    if (cw <= 2) { for (x = 0; x < width; x++) out[x] = t[x >> 1]; return; }                            /* libjpeg falls back to the box filter for planes no wider than 2 samples */
    o = plane + (uint32_t)(v ? (cr + 1 < ch ? cr + 1 : ch - 1) : (cr ? cr - 1 : 0)) * cs;              /* the nearest other row, the first and last rows replicated */
    for (x = 0; x < width; x++) {
        unsigned c = x >> 1; int32_t s = 3 * t[c] + o[c], v2;
        if (!(x & 1)) v2 = c == 0 ? (s * 4 + 8) >> 4 : (s * 3 + (3 * t[c - 1] + o[c - 1]) + 8) >> 4;
        else          v2 = c == cw - 1 ? (s * 4 + 7) >> 4 : (s * 3 + (3 * t[c + 1] + o[c + 1]) + 7) >> 4;
        out[x] = (uint8_t)v2;
    }
}
int tgy_tile_rgb(tgy_dec_t *d, tgy_planes_t *pl, const uint8_t *stream, uint32_t len, unsigned sub, const uint8_t *rows, const uint8_t *cols, unsigned w, unsigned h,
                 const uint16_t *qy, const uint16_t *qc, tgy_row_fn emit, void *ctx)
{
    unsigned ncomp = sub == TGY_GREY ? 1u : 3u, ci, y, x, r, i; int16_t blk[64]; uint8_t px[64], row[128 * 3];
    tgy_tile_begin(d, stream, len);
    for (ci = 0; ci < ncomp; ci++) {
        unsigned cc = ci ? 1u : 0u, nr = rows[ci], nc = cols[ci], stride = ci == 0 || sub == TGY_444 ? 128u : 64u; int32_t dcrow[16], dcleft = 0; uint8_t *plane = ci == 0 ? pl->y : pl->c[ci - 1];
        for (y = 0; y < nr; y++)
            for (x = 0; x < nc; x++) {
                int32_t pred = x ? dcleft : (y ? dcrow[x] : 0);                                         /* left block of the row, else the block above, else 0 */
                tgy_block_pred(d, cc, pred, blk); if (d->err) return 1;
                dcleft = blk[0]; dcrow[x] = blk[0];
                tgy_dequant_idct(blk, ci ? qc : qy, px);
                for (r = 0; r < 8; r++) for (i = 0; i < 8; i++) plane[(y * 8 + r) * stride + x * 8 + i] = px[r * 8 + i];
            }
    }
    {
        unsigned cw = (w + 1) / 2, chh = (h + 1) / 2;
        for (r = 0; r < h; r++) {
            uint8_t cbr[128], crr[128];
            if (sub == TGY_GREY) { for (x = 0; x < w; x++) row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = pl->y[r * 128 + x]; }
            else {
                if (sub == TGY_420) { tgy_upsample_row(pl->c[0], 64, cw, chh, r, w, cbr); tgy_upsample_row(pl->c[1], 64, cw, chh, r, w, crr); }
                else for (x = 0; x < w; x++) { cbr[x] = pl->c[0][r * 128 + x]; crr[x] = pl->c[1][r * 128 + x]; }
                for (x = 0; x < w; x++) tgy_ycc_to_rgb(pl->y[r * 128 + x], cbr[x], crr[x], row + 3 * x);
            }
            emit(ctx, r, row);
        }
    }
    return 0;
}
