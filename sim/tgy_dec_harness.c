/* Harness of fw2/gallery/tgy_dec.c: `tgy_dec_harness vectors FILE` decodes every tile of a binary vector file and compares with the expected blocks; `tgy_dec_harness fuzz SEED N` decodes N random byte strings of random
 * lengths and shapes (the decoder must terminate and stay inside its buffers: build with the sanitizers). Binary file: u32 count, then per vector: u32 ncomp, ncomp x (u32 rows, u32 cols), u32 stream length, the stream,
 * u32 block count, block count x 64 x int16 (little-endian, the order comp, row, col). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/gallery/tgy_dec.c"
static uint32_t rd32(FILE *f) { uint8_t b[4]; if (fread(b, 1, 4, f) != 4) exit(2); return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static tgy_dec_t dec;
int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "vectors")) {
        FILE *f = fopen(argv[2], "rb"); if (!f) return 2; uint32_t n = rd32(f), bad = 0;
        for (uint32_t v = 0; v < n; v++) {
            uint8_t rows[3] = {0}, cols[3] = {0}; uint32_t nc = rd32(f);
            for (uint32_t i = 0; i < nc; i++) { rows[i] = (uint8_t)rd32(f); cols[i] = (uint8_t)rd32(f); }
            uint32_t sl = rd32(f); uint8_t *s = malloc(sl ? sl : 1); if (sl && fread(s, 1, sl, f) != sl) return 2;
            uint32_t nb = rd32(f); int16_t *want = malloc((size_t)nb * 128), *got = calloc((size_t)nb * 64 + 64, 2);
            if (fread(want, 2, (size_t)nb * 64, f) != (size_t)nb * 64) return 2;
            int err = tgy_tile(&dec, s, sl, nc, rows, cols, got);
            int ok = !err && !memcmp(want, got, (size_t)nb * 128);
            uint32_t sl2 = sl; while (sl2 && s[sl2 - 1] == 0) sl2--;                                   /* bytes past the end of a stream read as zero: trailing zero bytes may be dropped */
            if (sl2 != sl) { memset(got, 0, (size_t)nb * 128); int e2 = tgy_tile(&dec, s, sl2, nc, rows, cols, got); ok = ok && !e2 && !memcmp(want, got, (size_t)nb * 128); }
            printf("vector %u %s\n", v, ok ? "ok" : "FAIL"); bad += !ok; free(s); free(want); free(got);
        }
        printf(bad ? "TGY DEC FAILED %u\n" : "TGY DEC PASSED (0 failures)\n", bad); return bad != 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "engine")) {                                               /* a context driven by 300 zeros, then 300 ones, then 300 zeros: the saturation points and the count */
        tgy_tile_begin(&dec, (const uint8_t *)"", 0);
        for (int ph = 0; ph < 3; ph++) { for (int i = 0; i < 300; i++) update(&dec, 7, ph == 1); printf("%u %u\n", dec.prob[7], dec.cnt[7]); }
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "fuzz")) {
        uint64_t x = strtoull(argv[2], 0, 10) * 2654435761u + 88172645463325252ull; unsigned n = (unsigned)atoi(argv[3]), errs = 0;
        static int16_t out[(256 + 64 + 64) * 64 + 64]; static uint8_t buf[4096];
        #define RND() (x ^= x << 13, x ^= x >> 7, x ^= x << 17, x)
        for (unsigned t = 0; t < n; t++) {
            unsigned len = (unsigned)(RND() % 3000), mode = (unsigned)(RND() % 4), nc = 1 + (unsigned)(RND() % 3); uint8_t rows[3], cols[3];
            for (unsigned i = 0; i < len; i++) buf[i] = mode == 0 ? 0xFF : (mode == 1 ? 0x00 : (uint8_t)RND());
            unsigned tot = 0; for (unsigned i = 0; i < nc; i++) { rows[i] = (uint8_t)(1 + RND() % 8); cols[i] = (uint8_t)(1 + RND() % 8); tot += rows[i] * cols[i]; }
            errs += (unsigned)tgy_tile(&dec, buf, len, nc, rows, cols, out); (void)tot;
        }
        printf("fuzz done: %u streams, %u flagged impossible\n", n, errs); return 0;
    }
    return 2;
}
