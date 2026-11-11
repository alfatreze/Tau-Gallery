/* Harness of fw2/gallery/tgy_pix.c. `pictures FILE`: for each picture (w, h, sub, quantisation tables, tiles with their streams) decode every tile and compare the assembled RGB with the expected bytes; `prims FILE`: the IDCT, colour
 * and upsampling vectors. Binary little-endian, see sim/test_tgy_pix_c.py for the writer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/gallery/tgy_dec.c"
#include "../fw2/gallery/tgy_pix.c"
static uint32_t rd32(FILE *f) { uint8_t b[4]; if (fread(b, 1, 4, f) != 4) exit(2); return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void rdb(FILE *f, void *p, size_t n) { if (n && fread(p, 1, n, f) != n) exit(2); }
static tgy_dec_t dec; static tgy_planes_t planes;
typedef struct { uint8_t *canvas; unsigned w, tx, ty; } canvas_t;
static void emit(void *ctx, unsigned row, const uint8_t *rgb)
{
    canvas_t *c = ctx; unsigned tw = c->w - c->tx * 128; if (tw > 128) tw = 128;
    memcpy(c->canvas + ((size_t)(c->ty * 128 + row) * c->w + c->tx * 128) * 3, rgb, (size_t)tw * 3);
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2; FILE *f = fopen(argv[2], "rb"); if (!f) return 2; uint32_t bad = 0;
    if (!strcmp(argv[1], "pictures")) {
        uint32_t np = rd32(f);
        for (uint32_t p = 0; p < np; p++) {
            uint32_t w = rd32(f), h = rd32(f), sub = rd32(f); uint16_t qy[64], qc[64]; rdb(f, qy, 128); rdb(f, qc, 128);
            uint8_t *canvas = calloc((size_t)w * h * 3 + 1, 1), *want = malloc((size_t)w * h * 3 + 1); uint32_t nt = rd32(f); int err = 0;
            for (uint32_t t = 0; t < nt; t++) {
                uint32_t ty = rd32(f), tx = rd32(f), nc = rd32(f), len; uint8_t rows[3] = {0}, cols[3] = {0};
                for (uint32_t i = 0; i < nc; i++) { rows[i] = (uint8_t)rd32(f); cols[i] = (uint8_t)rd32(f); }
                len = rd32(f); uint8_t *s = malloc(len ? len : 1); rdb(f, s, len);
                unsigned tw = w - tx * 128 < 128 ? w - tx * 128 : 128, th = h - ty * 128 < 128 ? h - ty * 128 : 128; canvas_t cv = { canvas, w, tx, ty };
                err |= tgy_tile_rgb(&dec, &planes, s, len, sub, rows, cols, tw, th, qy, qc, emit, &cv); free(s);
            }
            rdb(f, want, (size_t)w * h * 3); int ok = !err && !memcmp(canvas, want, (size_t)w * h * 3); size_t firstbad = 0;
            if (!ok) for (; firstbad < (size_t)w * h * 3 && canvas[firstbad] == want[firstbad]; firstbad++) ;
            printf("picture %u (%ux%u sub %u, %u tiles) %s", p, w, h, sub, nt, ok ? "ok\n" : "FAIL"); if (!ok) printf(" err %d first differing byte %zu\n", err, firstbad); bad += !ok; free(canvas); free(want);
        }
    } else if (!strcmp(argv[1], "prims")) {
        uint32_t n = rd32(f);
        for (uint32_t i = 0; i < n; i++) { int32_t in[64]; uint8_t want[64], got[64]; rdb(f, in, 256); rdb(f, want, 64); tgy_idct_natural(in, got); if (memcmp(want, got, 64)) { printf("idct %u FAIL\n", i); bad++; } }
        n = rd32(f);
        for (uint32_t i = 0; i < n; i++) { uint32_t v[6]; uint8_t rgb[3]; for (int k = 0; k < 6; k++) v[k] = rd32(f); tgy_ycc_to_rgb(v[0], v[1], v[2], rgb); if (rgb[0] != v[3] || rgb[1] != v[4] || rgb[2] != v[5]) { printf("colour %u FAIL\n", i); bad++; } }
        n = rd32(f);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t h = rd32(f), w = rd32(f); uint8_t *pl = malloc((size_t)w * h), *want = malloc((size_t)w * h * 4), *got = calloc((size_t)w * h * 4 + 1, 1); rdb(f, pl, (size_t)w * h); rdb(f, want, (size_t)w * h * 4);
            for (uint32_t ty = 0; ty < h; ty += 64) for (uint32_t tx = 0; tx < w; tx += 64) {                          /* the tile profile: each 64 x 64 chroma tile on its own */
                uint32_t th = h - ty < 64 ? h - ty : 64, tw = w - tx < 64 ? w - tx : 64;
                for (uint32_t r = 0; r < 2 * th; r++) tgy_upsample_row(pl + (size_t)ty * w + tx, w, tw, th, r, 2 * tw, got + (size_t)(2 * ty + r) * (2 * w) + 2 * tx);
            }
            if (memcmp(want, got, (size_t)w * h * 4)) { printf("upsample %u (%ux%u) FAIL\n", i, w, h); bad++; } free(pl); free(want); free(got);
        }
    } else return 2;
    printf(bad ? "TGY PIX FAILED %u\n" : "TGY PIX PASSED (0 failures)\n", bad); return bad != 0;
}
