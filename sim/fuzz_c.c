/* Mutation fuzzer of the firmware's parsers and the TGY decoder (docs/FULLCORE_SIM.md, CLAUDE.md testing rule 1): real files are mutated (bit flips, bytes set to 0x00 / 0xFF, 32-bit fields overwritten with edge values, truncation,
 * random splices) and fed to the code the firmware runs on files it did not write. Built with AddressSanitizer and UBSan by sim/test_fuzz_c.py; a deterministic xorshift generator, so a failure repeats.
 *   fuzz_c container FILE ITERS   tgy_open / tgy_level / tgy_load_index / tgy_hw_level (against a register model): never a crash, and the decoder is NEVER told to read outside the resident bytes of the file
 *   fuzz_c v0 FILE ITERS          gt_parse / gt_zoom_level on a version 0 file
 *   fuzz_c index FILE ITERS       gx_parse_head / gx_parse_entry on the folder index
 *   fuzz_c stream FILE ITERS      tgy_tile_rgb on mutated tile streams (bounded time, no write outside the planes), tables and geometry from the container
 * Prints "FUZZ ok <mode> <iterations> <accepted>" and exits 0, or the failing iteration and exit 1. */
#define P2_HOST 1
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
uint32_t REG_RD(uint32_t a);
void REG_WR(uint32_t a, uint32_t v);
#include "../fw2/platform/mmio.h"
#include "../fw2/gallery/tgy_dec.c"
#include "../fw2/gallery/tgy_pix.c"
static struct { uint32_t src, len, dst, pitch, geom, qy, qc; uint32_t starts; int bad; } hw;
static uint32_t cyc;
static uint32_t g_base, g_n;                               /* the resident bytes of the file the driver was told about */
uint32_t REG_RD(uint32_t a) { if (a == R_CYCLES) { cyc += 50000; return cyc; } if (a == R_TGY_STATUS) return 0x8000000Au; return 0; }
void REG_WR(uint32_t a, uint32_t v)
{
    switch (a) {
    case R_TGY_SRC: hw.src = v; break; case R_TGY_LEN: hw.len = v; break; case R_TGY_DST: hw.dst = v; break; case R_TGY_PITCH: hw.pitch = v; break; case R_TGY_GEOM: hw.geom = v; break;
    case R_TGY_QY: hw.qy = v; break; case R_TGY_QC: hw.qc = v; break;
    case R_TGY_CTRL:
        if (v & 2) { if (hw.qy < g_base || hw.qy + 128u > g_base + g_n || hw.qc < g_base || hw.qc + 128u > g_base + g_n || ((hw.qy | hw.qc) & 1u)) hw.bad |= 1; }           /* the tables read by the DMA must lie inside the file */
        if (v & 1) { hw.starts++; if ((uint64_t)hw.src + hw.len > (uint64_t)g_base + g_n || hw.src < g_base) hw.bad |= 2; if (hw.len >= (1u << 20)) hw.bad |= 4; }       /* and so must every stream */
        break;
    default: break;
    }
}
#include "../fw2/gallery/gallery_fmt.c"
#include "../fw2/gallery/tgy_hw.c"
static unsigned g_sink_w, g_sink_rows; static volatile uint8_t g_sink_sum;
static void sink(void *c, unsigned row, const uint8_t *rgb) { (void)c; (void)row; uint8_t x = 0; for (unsigned i = 0; i < g_sink_w * 3u; i++) x ^= rgb[i]; g_sink_sum = x; g_sink_rows++; }      /* reads every byte: AddressSanitizer sees a short row */
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static const uint32_t EDGE[] = { 0u, 1u, 2u, 3u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu, 4095u, 4096u, 4097u, 0xFFFFF000u };
static void mutate(uint8_t *b, uint32_t n, uint32_t focus)       /* focus: mutations concentrate in the first `focus` bytes half of the time */
{
    int k = 1 + (int)(rnd() % 4);
    while (k--) {
        uint32_t lim = (focus && (rnd() & 1)) ? (focus < n ? focus : n) : n; if (!lim) return;
        uint32_t p = rnd() % lim;
        switch (rnd() % 6) {
        case 0: b[p] ^= (uint8_t)(1u << (rnd() & 7)); break;
        case 1: b[p] = 0; break; case 2: b[p] = 0xFF; break;
        case 3: if (p + 4 <= n) { uint32_t v = EDGE[rnd() % (sizeof EDGE / sizeof EDGE[0])]; memcpy(b + p, &v, 4); } break;
        case 4: b[p] = (uint8_t)rnd(); break;
        default: if (p + 8 <= n) { uint32_t q = rnd() % (n - 7); memcpy(b + p, b + q, 8); } break;
        }
    }
}
static uint8_t *slurp(const char *p, uint32_t *n) { FILE *f = fopen(p, "rb"); if (!f) exit(2); fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)l + 8); if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(2); fclose(f); *n = (uint32_t)l; return b; }
/* the buffer a parser is given sits at the start of a 5 GiB reservation that nothing else may touch: an offset taken from the file that wrapped around 2^32 (or any wild read) faults at once,
   instead of landing on whatever happens to be mapped 4 GB away */
static uint8_t *guarded(size_t n)
{
    size_t res = (size_t)5 << 30, rw = (n + 16383u) & ~(size_t)16383u;
    uint8_t *p = mmap(0, res, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED || mprotect(p, rw, PROT_READ | PROT_WRITE)) { perror("mmap"); exit(2); }
    return p;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* structure aware: mutate whole 32 bit fields of the header and the level table (values at the edges, or the old value +-1..64), then repair the CRCs so the
   mutation gets past the checksums and reaches the range checks (a blind byte flip is rejected by the header CRC and never tests them) */
static void smut(uint8_t *w, uint32_t m, const uint8_t *orig, int v1)
{
    uint32_t nlev = le32(orig + 20), lt = le32(orig + 24); if (nlev > 16u) nlev = 16u;
    int k = 1 + (int)(rnd() % 3);
    while (k--) {
        uint32_t span = 44u + nlev * 48u, q = (rnd() % span) & ~3u, off = q < 44u ? 8u + q : lt + (q - 44u);
        if (off + 4u > m) continue;
        if (!(rnd() % 4)) off = 20u + 4u * (rnd() % 3);                      /* the level count, the level table offset, ... */
        uint32_t v = le32(orig + off), c = rnd() % 5; if (c == 4) v = 0u - (1u + rnd() % (off == 24u ? (nlev ? nlev : 1u) * 48u : 2048u));   /* just below 2^32: table offset + count * 48 wraps around */ else if (c == 0) v = EDGE[rnd() % (sizeof EDGE / sizeof EDGE[0])]; else if (c == 1) v += 1 + rnd() % 64; else if (c == 2) v -= 1 + rnd() % 64; else v ^= 1u << (rnd() & 31);
        put32(w + off, v);
    }
    nlev = le32(w + 20); lt = le32(w + 24);
    if (nlev >= 1u && nlev <= 16u && lt >= 64u && lt <= 4096u && nlev * 48u <= 4096u - lt && m >= 4096u) {
        for (uint32_t i = 0; i < nlev; i++) { uint8_t *e = w + lt + i * 48u; uint32_t io = le32(e + 32), il = le32(e + 36); if (v1 && il <= 4096u && (uint64_t)io + il <= m) put32(e + 40, gf_crc32(w + io, il)); }
    }
    put32(w + 52, gf_crc32(w, 52u));
}
/* what an accepted header must satisfy, in 64 bit arithmetic: the level table (and for v1 the quantisation tables) lie inside the bytes the parser was given */
static int table_ok(const uint8_t *w, uint32_t avail, int v1)
{
    uint64_t nlev = le32(w + 20), lt = le32(w + 24), tab = le32(w + 40), tlen = le32(w + 44);
    if (lt + nlev * 48u > avail) return 0;
    if (v1 && (tab + tlen > avail || tab + tlen > 4096u)) return 0;
    return 1;
}
static const uint8_t *g_file; static uint32_t g_flen;
static int rd_file(void *c, uint32_t off, uint32_t len, uint8_t *dst) { (void)c; if ((uint64_t)off + len > g_flen) return 0; memcpy(dst, g_file + off, len); return 1; }
static int fail(const char *what, uint32_t it) { printf("FUZZ FAIL %s at iteration %u (seed state %llx)\n", what, it, (unsigned long long)rs); return 1; }
static void on_alarm(int x) { (void)x; printf("FUZZ FAIL a tile took more than 20 s (a hang)\n"); fflush(stdout); _exit(1); }
int main(int argc, char **argv)
{
    signal(SIGALRM, on_alarm);
    if (argc < 4) return 2; const char *mode = argv[1]; uint32_t n; uint8_t *orig = slurp(argv[2], &n); uint32_t iters = (uint32_t)strtoul(argv[3], 0, 10), acc = 0;
    uint8_t *w = guarded((size_t)n + 4096u); static uint8_t ibuf[4096]; static tgy_dec_t dec; static tgy_planes_t pl; static uint8_t sbuf[1u << 17];
    for (uint32_t it = 0; it < iters; it++) {
        memcpy(w, orig, n); uint32_t m = n; if (it) { mutate(w, n, 4096u + 2048u); if (rnd() % 16 == 0) m = rnd() % (n + 1); if ((!strcmp(mode, "container") || !strcmp(mode, "v0")) && n >= 4096u && (rnd() % 4)) { memcpy(w, orig, n); smut(w, n, orig, !strcmp(mode, "container")); } }          /* iteration 0 is the unmutated file */
        if (!strcmp(mode, "container")) {
            tgy_file_t f; tgy_level_t l; g_file = w; g_flen = m; g_base = (rnd() & 1) ? 0x800000u : 0x800000u + 2u * (rnd() % 64); g_n = m; memset(&hw, 0, sizeof hw);
            if (m < 4096u) continue;
            if (tgy_open(&f, w, m, g_base) != TH_OK) continue;
            if (!table_ok(w, m, 1)) return fail("tgy_open accepted tables outside the first 4 KB or outside the file", it);
            if (f.n > f.size) return fail("an open accepted more resident bytes than the file's size", it);
            for (uint32_t i = 0; i < f.nlevels; i++) {
                if (tgy_level(&f, i, &l) != TH_OK) continue;
                if (l.qy >= f.ntables || l.qc >= f.ntables) return fail("a level names a quantisation table the file does not have", it);
                if (l.data_off + l.data_len > m || l.idx_off + l.idx_len > m) return fail("a level the driver accepted lies outside the file", it);
                if (tgy_load_index(&f, &l, rd_file, 0, ibuf, sizeof ibuf) != TH_OK) continue;
                acc++; tgy_hw_forget(); int r = tgy_hw_level(&f, &l, 0x400000u, 2400u, 0, 0); (void)r;
                if (hw.bad) return fail(hw.bad & 1 ? "a table address outside the file" : hw.bad & 2 ? "a stream outside the file" : "a stream length of 1 MiB or more", it);
            }
        } else if (!strcmp(mode, "v0")) {
            gt_info_t t; gt_zoom_t z; if (gt_parse(w, m < 4096 ? m : 4096u, &t) == GF_OK) { acc++; if (!table_ok(w, m < 4096 ? m : 4096u, 0)) return fail("gt_parse accepted a level table outside its buffer", it); if ((uint64_t)t.canvas_off + t.canvas_len > t.file_size) return fail("a canvas outside the file", it); }
            for (uint32_t k = 0; k < 8; k++) if (gt_zoom_level(w, m < 4096 ? m : 4096u, k, &z) == GF_OK) { acc++; if (!table_ok(w, m < 4096 ? m : 4096u, 0)) return fail("gt_zoom_level accepted a level table outside its buffer", it); if ((uint64_t)z.off + z.len > (uint64_t)((uint32_t)w[48] | (uint32_t)w[49] << 8 | (uint32_t)w[50] << 16 | (uint32_t)w[51] << 24)) return fail("a zoom level outside the file", it); }
        } else if (!strcmp(mode, "index")) {
            gx_head_t h; gx_entry_t e; if (gx_parse_head(w, m, &h) == GF_OK) { acc++; for (uint32_t i = 0; i < 4 && i < h.count; i++) { uint32_t o = gx_entry_off(&h, i); if ((uint64_t)o + GX_ENTRY_BYTES <= m) (void)gx_parse_entry(w + o, GX_ENTRY_BYTES, &e); } }
        } else if (!strcmp(mode, "stream")) {
            tgy_file_t f; tgy_level_t l; g_file = orig; g_flen = n; if (tgy_open(&f, orig, n, 0x800000u) != TH_OK) return 2;
            if (tgy_find_level(&f, 2u, &l) != TH_OK && tgy_find_level(&f, 1u, &l) != TH_OK) return 2;
            if (tgy_load_index(&f, &l, rd_file, 0, ibuf, sizeof ibuf) != TH_OK) return 2;
            uint32_t t = rnd() % (l.cols * l.rows), tx = t % l.cols, ty = t / l.cols; const uint8_t *e = ibuf + 12u * t; uint32_t off = e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24, len = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
            if (len > sizeof sbuf) len = sizeof sbuf; memcpy(sbuf, orig + l.data_off + off, len); if (it) { for (int k = 0; k < 1 + (int)(rnd() % 6); k++) { if (!len) break; uint32_t p = rnd() % len; sbuf[p] = (rnd() & 1) ? (uint8_t)rnd() : (uint8_t)(sbuf[p] ^ (1u << (rnd() & 7))); } if (rnd() % 8 == 0) len = rnd() % (len + 1); }
            unsigned wd = l.w - tx * 128 < 128 ? l.w - tx * 128 : 128, ht = l.h - ty * 128 < 128 ? l.h - ty * 128 : 128; uint8_t rows[3], cols[3]; unsigned fmt = l.fmt;
            if (fmt == 2) { rows[0] = (uint8_t)(2 * ((ht + 15) / 16)); cols[0] = (uint8_t)(2 * ((wd + 15) / 16)); rows[1] = rows[2] = (uint8_t)((ht + 15) / 16); cols[1] = cols[2] = (uint8_t)((wd + 15) / 16); }
            else { rows[0] = rows[1] = rows[2] = (uint8_t)((ht + 7) / 8); cols[0] = cols[1] = cols[2] = (uint8_t)((wd + 7) / 8); }
            uint16_t qy[64], qc[64]; for (int k = 0; k < 64; k++) { uint32_t o = f.tables_off + 2u + 128u * l.qy + 2u * (uint32_t)k, o2 = f.tables_off + 2u + 128u * l.qc + 2u * (uint32_t)k; qy[k] = (uint16_t)(orig[o] | orig[o + 1] << 8); qc[k] = (uint16_t)(orig[o2] | orig[o2 + 1] << 8); }
            if (!(rnd() % 4)) for (int k = 0; k < 64; k++) { qy[k] = (uint16_t)(1 + rnd() % 65535); qc[k] = (uint16_t)(1 + rnd() % 65535); }       /* any table the format allows */
            int ones = 0; if (it && !(rnd() % 8)) { static const uint8_t F[] = { 0xFF, 0xFE, 0xF0, 0x80, 0x01 }; len = sizeof sbuf; ones = rnd() % sizeof F; memset(sbuf, F[ones], len); ones = (ones == 0); }   /* a run of ones decodes for ~5000 symbols a byte: the exponent cap is what stops it */
            alarm(20);
            g_sink_w = wd; g_sink_rows = 0; int r = tgy_tile_rgb(&dec, &pl, sbuf, len, fmt, rows, cols, wd, ht, qy, qc, sink, 0);
            if (ones && !r) return fail("a run of ones (an exponent far beyond the cap) was accepted as a tile", it);
            if (!r && g_sink_rows != ht) return fail("a decoded tile did not emit every row", it);
            (void)r; acc++;
        } else return 2;
    }
    printf("FUZZ ok %s %u %u\n", mode, iters, acc); return 0;
}
