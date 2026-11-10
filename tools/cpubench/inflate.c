/* A compact DEFLATE (RFC 1951) decoder and the PNG row filters, freestanding, written for the CPU benchmark of docs/M3_STREAMING_NOTE.md (software PNG on the VexRiscv). Table-driven Huffman decoding
 * (a 9-bit primary table, canonical bit-by-bit fallback for longer codes), whole output in memory (so the window is the output itself). Not production code: no error recovery beyond returning 0. */
#include <stdint.h>
typedef struct { uint16_t count[16]; uint16_t sym[288]; uint16_t fast[512]; } huff_t;
typedef struct { const uint8_t *p, *end; uint32_t buf; int cnt; uint8_t *out, *o; } st_t;

static const uint16_t len_base[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t  len_extra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dist_base[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t  dist_extra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static const uint8_t  clen_order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static inline void need(st_t *s, int n) { while (s->cnt < n) { s->buf |= (uint32_t)(s->p < s->end ? *s->p++ : 0u) << s->cnt; s->cnt += 8; } }
static inline uint32_t bits(st_t *s, int n) { if (!n) return 0; need(s, n); uint32_t v = s->buf & ((1u << n) - 1u); s->buf >>= n; s->cnt -= n; return v; }

static void build(huff_t *h, const uint8_t *len, int n)
{
    uint16_t offs[16]; int i;
    for (i = 0; i < 16; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0; for (i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    for (i = 0; i < 512; i++) h->fast[i] = 0;
    { uint32_t code = 0; int k = 0;                                                        /* canonical codes in order of length, then symbol: fill the primary table for the short ones */
      for (int l = 1; l <= 15; l++) {
          for (int c = 0; c < h->count[l]; c++, k++, code++) {
              if (l > 9) continue;
              uint32_t r = 0; for (int b = 0; b < l; b++) if (code & (1u << b)) r |= 1u << (l - 1 - b);       /* bit-reversed: the stream is LSB first */
              for (uint32_t f = r; f < 512u; f += 1u << l) h->fast[f] = (uint16_t)(h->sym[k] | (l << 9));
          }
          code <<= 1;
      } }
}
static inline int decode(st_t *s, const huff_t *h)
{
    need(s, 15);
    uint16_t e = h->fast[s->buf & 511u];
    if (e) { int l = e >> 9; s->buf >>= l; s->cnt -= l; return e & 511; }
    { int code = 0, first = 0, index = 0;
      for (int l = 1; l <= 15; l++) {
          code |= (int)(s->buf & 1u); s->buf >>= 1; s->cnt--;
          int c = h->count[l];
          if (code - c < first) return h->sym[index + (code - first)];
          index += c; first += c; first <<= 1; code <<= 1;
      } }
    return -1;
}
static int codes(st_t *s, const huff_t *lit, const huff_t *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return 0;
        if (sym < 256) { *s->o++ = (uint8_t)sym; continue; }
        if (sym == 256) return 1;
        sym -= 257; if (sym >= 29) return 0;
        int len = len_base[sym] + (int)bits(s, len_extra[sym]);
        int ds = decode(s, dist); if (ds < 0 || ds >= 30) return 0;
        uint32_t d = dist_base[ds] + bits(s, dist_extra[ds]);
        if (d > (uint32_t)(s->o - s->out)) return 0;
        const uint8_t *src = s->o - d; while (len--) *s->o++ = *src++;
    }
}
/* Inflates the raw DEFLATE stream at p (after a 2-byte zlib header) into out; returns the number of bytes written, or 0 on an error. */
uint32_t inflate_raw(const uint8_t *p, uint32_t n, uint8_t *out)
{
    static huff_t lit, dist; st_t s = { p, p + n, 0, 0, out, out }; int last;
    do {
        last = (int)bits(&s, 1); int type = (int)bits(&s, 2);
        if (type == 0) {
            s.buf = 0; s.cnt = 0;                                                       /* byte-align: whole bytes are never held in buf beyond the current byte in this reader after a reset, see below */
            uint32_t ln = s.p[0] | (uint32_t)s.p[1] << 8; s.p += 4; while (ln--) *s.o++ = *s.p++;
        } else if (type == 1) {
            uint8_t l[320]; int i; for (i = 0; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
            build(&lit, l, 288); for (i = 0; i < 30; i++) l[i] = 5; build(&dist, l, 30);
            if (!codes(&s, &lit, &dist)) return 0;
        } else if (type == 2) {
            int nl = (int)bits(&s, 5) + 257, nd = (int)bits(&s, 5) + 1, nc = (int)bits(&s, 4) + 4; uint8_t l[320]; int i;
            for (i = 0; i < 19; i++) l[i] = 0; for (i = 0; i < nc; i++) l[clen_order[i]] = (uint8_t)bits(&s, 3);
            static huff_t cl; build(&cl, l, 19);
            uint8_t ll[320]; int k = 0;
            while (k < nl + nd) {
                int sym = decode(&s, &cl); if (sym < 0) return 0;
                if (sym < 16) ll[k++] = (uint8_t)sym;
                else { int rep, v = 0; if (sym == 16) { if (!k) return 0; v = ll[k - 1]; rep = 3 + (int)bits(&s, 2); } else if (sym == 17) rep = 3 + (int)bits(&s, 3); else rep = 11 + (int)bits(&s, 7);
                       if (k + rep > nl + nd) return 0; while (rep--) ll[k++] = (uint8_t)v; }
            }
            build(&lit, ll, nl); build(&dist, ll + nl, nd);
            if (!codes(&s, &lit, &dist)) return 0;
        } else return 0;
    } while (!last);
    return (uint32_t)(s.o - out);
}
/* The PNG row filters (all five), in place: data is rows of 1 filter byte plus w * bpp bytes; the result is packed rows of w * bpp bytes at the start of data. Returns the packed size. */
static inline int paeth(int a, int b, int c) { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p; return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
uint32_t png_unfilter(uint8_t *data, uint32_t w, uint32_t h, uint32_t bpp)
{
    const uint32_t stride = w * bpp; uint8_t *dst = data;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = data + y * (stride + 1u); const uint8_t ft = src[0]; src++;
        const uint8_t *prior = y ? dst - stride : 0;
        switch (ft) {
        case 0: for (uint32_t i = 0; i < stride; i++) dst[i] = src[i]; break;
        case 1: for (uint32_t i = 0; i < stride; i++) dst[i] = (uint8_t)(src[i] + (i >= bpp ? dst[i - bpp] : 0)); break;
        case 2: for (uint32_t i = 0; i < stride; i++) dst[i] = (uint8_t)(src[i] + (prior ? prior[i] : 0)); break;
        case 3: for (uint32_t i = 0; i < stride; i++) { int a = i >= bpp ? dst[i - bpp] : 0, b = prior ? prior[i] : 0; dst[i] = (uint8_t)(src[i] + ((a + b) >> 1)); } break;
        default: for (uint32_t i = 0; i < stride; i++) { int a = i >= bpp ? dst[i - bpp] : 0, b = prior ? prior[i] : 0, c = (i >= bpp && prior) ? prior[i - bpp] : 0; dst[i] = (uint8_t)(src[i] + paeth(a, b, c)); } break;
        }
        dst += stride;
    }
    return stride * h;
}
