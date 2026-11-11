#include "tgy_dec.h"

#define LIMIT 30u
static const uint16_t REC[31] = { 32768, 26214, 18724, 14563, 11915, 10082, 8738, 7710, 6898, 6241, 5698, 5242, 4854, 4519, 4228, 3971, 3744, 3542, 3360, 3196, 3048, 2912, 2788, 2674, 2570, 2473, 2383, 2299, 2221, 2148, 2080 };
static const uint8_t KG[64] = { 0, 0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 };
static const int8_t LN[64][2] = { {-1, -1}, {0, -1}, {-1, 0}, {-1, 2}, {2, 1}, {1, -1}, {5, -1}, {4, 5}, {3, 4}, {-1, 3}, {-1, 9}, {9, 8}, {8, 7}, {7, 6}, {6, -1}, {14, -1}, {13, 14}, {12, 13}, {11, 12}, {10, 11}, {-1, 10}, {-1, 20}, {20, 19}, {19, 18}, {18, 17}, {17, 16}, {16, 15}, {15, -1}, {27, -1}, {26, 27}, {25, 26}, {24, 25}, {23, 24}, {22, 23}, {21, 22}, {-1, 21}, {35, 34}, {34, 33}, {33, 32}, {32, 31}, {31, 30}, {30, 29}, {29, 28}, {41, 42}, {40, 41}, {39, 40}, {38, 39}, {37, 38}, {36, 37}, {48, 47}, {47, 46}, {46, 45}, {45, 44}, {44, 43}, {52, 53}, {51, 52}, {50, 51}, {49, 50}, {57, 56}, {56, 55}, {55, 54}, {59, 60}, {58, 59}, {62, 61} };
enum { O_D0 = 0, O_DS = 2, O_DX = 4, O_DM = 24, O_LT = 44, O_SG = 170, O_G1 = 300, O_MX = 430, O_MM = 1230 };

static uint8_t next_byte(tgy_dec_t *d) { return d->p < d->end ? *d->p++ : 0u; }
static void normalise(tgy_dec_t *d)
{
    while (d->range < (1u << 24)) { d->range <<= 8; d->code = (d->code << 8) | next_byte(d); }
}
static void update(tgy_dec_t *d, unsigned c, unsigned bit)
{
    uint32_t p = d->prob[c], r = REC[d->cnt[c]];
    p = bit ? p - ((p * r) >> 16) : p + (((32768u - p) * r) >> 16);
    d->prob[c] = (uint16_t)(p < 32u ? 32u : (p > 32768u - 32u ? 32768u - 32u : p));
    if (d->cnt[c] < LIMIT) d->cnt[c]++;
}
static unsigned ctx_bit(tgy_dec_t *d, unsigned c)
{
    uint32_t bound = (d->range >> 15) * d->prob[c]; unsigned b;
    if (d->code >= bound) { d->code -= bound; d->range -= bound; b = 1; } else { d->range = bound; b = 0; }
    update(d, c, b); normalise(d); return b;
}
static unsigned bypass_bit(tgy_dec_t *d)
{
    unsigned b; d->range >>= 1;
    if (d->code >= d->range) { d->code -= d->range; b = 1; } else b = 0;
    normalise(d); return b;
}
void tgy_tile_begin(tgy_dec_t *d, const uint8_t *data, uint32_t len)
{
    unsigned i;
    d->p = data; d->end = data + len; d->range = 0xFFFFFFFFu; d->code = 0; d->err = 0;
    for (i = 0; i < TGY_N_CTX; i++) { d->prob[i] = 16384u; d->cnt[i] = 0; }
    for (i = 0; i < 5; i++) d->code = (d->code << 8) | next_byte(d);
}
/* Exp-Golomb order 0: the exponent in unary with contexts xbase + min(i, 9), the first mantissa bit in context mbase + min(e, 9), the rest bypass. Returns m. */
static uint32_t exp_golomb(tgy_dec_t *d, unsigned xbase, unsigned mbase)
{
    unsigned e = 0, j; uint32_t v = 1;
    while (ctx_bit(d, xbase + (e < 9u ? e : 9u))) { if (++e > TGY_MAX_EXP) { d->err = 1; return 0; } }
    for (j = e; j > 0; j--) v = (v << 1) | (j == e ? ctx_bit(d, mbase + (e < 9u ? e : 9u)) : bypass_bit(d));
    return v - 1u;
}
static int16_t sat(int32_t v) { return (int16_t)(v > 32767 ? 32767 : (v < -32767 ? -32767 : v)); }
static unsigned lbk(unsigned x) { return x == 0 ? 0u : (x == 1 ? 1u : (x == 2 ? 2u : (x <= 4 ? 3u : 4u))); }
static unsigned iabs(int v) { return (unsigned)(v < 0 ? -v : v); }

void tgy_block_pred(tgy_dec_t *d, unsigned cc, int32_t pred, int16_t *out)
{
    int32_t dc = 0; unsigned node = 1, last, k, i;
    for (i = 0; i < 64; i++) out[i] = 0;
    if (ctx_bit(d, O_D0 + cc)) {
        unsigned neg = ctx_bit(d, O_DS + cc); int32_t a = (int32_t)exp_golomb(d, O_DX + cc * 10u, O_DM + cc * 10u) + 1;
        dc = neg ? -a : a;
    }
    out[0] = sat(pred + dc);
    for (i = 0; i < 6; i++) node = node * 2u + ctx_bit(d, O_LT + cc * 63u + node - 1u);
    last = node - 64u;
    for (k = 1; k <= last; k++) {
        int li = LN[k][0], ui = LN[k][1]; unsigned lc, a; int32_t v;
        if (li >= 0 && ui >= 0) lc = lbk(iabs(out[li]) + iabs(out[ui])); else if (li >= 0) lc = lbk(iabs(out[li])); else if (ui >= 0) lc = lbk(iabs(out[ui])); else lc = 0;
        if (k < last && !ctx_bit(d, O_SG + (cc * 13u + KG[k]) * 5u + lc)) continue;
        if (ctx_bit(d, O_G1 + (cc * 13u + KG[k]) * 5u + lc)) {
            unsigned kg = KG[k] < 7u ? KG[k] : 7u;
            a = exp_golomb(d, O_MX + ((cc * 8u + kg) * 5u + lc) * 10u, O_MM + cc * 10u) + 2u;
        } else a = 1;
        v = (int32_t)(a > 32767u ? 32767u : a); out[k] = sat(bypass_bit(d) ? -v : v);
        if (d->err) return;
    }
}
void tgy_block(tgy_dec_t *d, unsigned cc, const int16_t *left, const int16_t *above, int16_t *out)
{
    tgy_block_pred(d, cc, left ? left[0] : (above ? above[0] : 0), out);
}
int tgy_tile(tgy_dec_t *d, const uint8_t *data, uint32_t len, unsigned ncomp, const uint8_t *rows, const uint8_t *cols, int16_t *out)
{
    unsigned ci, y, x; int16_t *base = out;
    tgy_tile_begin(d, data, len);
    for (ci = 0; ci < ncomp; ci++) {
        unsigned cc = ci ? 1u : 0u, r = rows[ci], c = cols[ci];
        for (y = 0; y < r; y++)
            for (x = 0; x < c; x++) {
                int16_t *blk = base + ((uint32_t)y * c + x) * 64u;
                tgy_block(d, cc, x ? blk - 64 : 0, y ? blk - (int32_t)c * 64 : 0, blk);
                if (d->err) return 1;
            }
        base += (uint32_t)r * c * 64u;
    }
    return 0;
}
