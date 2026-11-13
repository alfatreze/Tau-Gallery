/* Host harness of fw2/gallery/tgy_hw.c: the driver runs against a MODEL of the hardware decoder (registers 0x220-0x244 as tau_tgy_regs.sv defines them) over a byte-addressed memory standing for the SDRAM. The model decodes with the
 * C reference pipeline (tgy_dec.c / tgy_pix.c) where the hardware would, reads the tables and the stream from the memory at the programmed byte addresses (an odd stream address, an odd pitch work as in the RTL), writes the
 * RGB rows at dst + row * pitch byte for byte, keeps `busy` for a few polls, sets done / err / rejected like the RTL, and records how the driver used the registers. Modes (argv): see sim/test_tgy_hw.py.
 *   tgy_hw_harness MODE FILE BASE OUT [args]   MODE: show (fit level on the canvas), full (the full level at dst0 with an odd pitch), badidx, badhdr, odd, absent, hwerr, busy, rejected */
#define P2_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
uint32_t REG_RD(uint32_t a);
void REG_WR(uint32_t a, uint32_t v);
#include "../fw2/platform/mmio.h"
#define MEM_SIZE (24u << 20)
static uint8_t *mem;
static struct { uint32_t src, len, dst, pitch, geom, qy, qc, status, cycles_reg, cyc, polls_left, irq; uint16_t ty[64], tc[64]; int present, force_busy, reject_next, tables_ok, starts, loads, bad_order, cfg_since_start; } hw;
#include "../fw2/gallery/tgy_dec.c"
#include "../fw2/gallery/tgy_pix.c"
static tgy_dec_t dec; static tgy_planes_t planes;
typedef struct { uint32_t dst, pitch; } emit_t;
static void emit(void *ctx, unsigned row, const uint8_t *rgb) { emit_t *e = ctx; unsigned w = (hw.geom >> 8) & 255; if ((uint64_t)e->dst + (uint64_t)row * e->pitch + w * 3u > MEM_SIZE) return; memcpy(mem + e->dst + (size_t)row * e->pitch, rgb, (size_t)w * 3u); }
static void run_tile(void)
{
    const unsigned fmt = (hw.geom >> 16) & 3, w = (hw.geom >> 8) & 255, h = hw.geom & 255; uint8_t rows[3] = {0}, cols[3] = {0}; unsigned nc = fmt == 0 ? 1 : 3;
    if (fmt == 2) { rows[0] = 2 * ((h + 15) / 16); cols[0] = 2 * ((w + 15) / 16); rows[1] = rows[2] = (h + 15) / 16; cols[1] = cols[2] = (w + 15) / 16; }
    else { rows[0] = rows[1] = rows[2] = (h + 7) / 8; cols[0] = cols[1] = cols[2] = (w + 7) / 8; }
    (void)nc; emit_t e = { hw.dst, hw.pitch };
    int err = (fmt == 3 || w == 0 || h == 0 || w > 128 || h > 128) ? 1 : tgy_tile_rgb(&dec, &planes, mem + hw.src, hw.len, fmt, rows, cols, w, h, hw.ty, hw.tc, emit, &e);
    hw.status = (hw.status & ~0x6u) | (err ? 4u : 2u) | (err ? (1u << 8) : 0u);
}
uint32_t REG_RD(uint32_t a)
{
    switch (a) {
    case R_CYCLES: hw.cyc += 1000; return hw.cyc;
    case R_TGY_STATUS: {
        uint32_t s = hw.status & ~1u; if (hw.polls_left > 0) { hw.polls_left--; s |= 1u; } if (hw.force_busy) s |= 1u;
        return (hw.present ? 0x80000000u : 0u) | s | (hw.tables_ok ? 8u : 0u); }
    case R_TGY_CYCLES: return 12345;
    case R_IRQ_PEND: return hw.irq;
    case R_TGY_SRC: return hw.src; case R_TGY_LEN: return hw.len; case R_TGY_DST: return hw.dst; default: return 0;
    }
}
void REG_WR(uint32_t a, uint32_t v)
{
    switch (a) {
    case R_TGY_SRC: hw.src = v & 0x3FFFFFF; hw.cfg_since_start++; break; case R_TGY_LEN: hw.len = v & 0xFFFFF; hw.cfg_since_start++; break; case R_TGY_DST: hw.dst = v & 0x3FFFFFF; hw.cfg_since_start++; break;
    case R_TGY_PITCH: hw.pitch = v & 0xFFFF; hw.cfg_since_start++; break; case R_TGY_GEOM: hw.geom = v & 0x3FFFF; hw.cfg_since_start++; break;
    case R_TGY_QY: hw.qy = v & 0x3FFFFFF; hw.cfg_since_start++; break; case R_TGY_QC: hw.qc = v & 0x3FFFFFF; hw.cfg_since_start++; break;
    case R_IRQ_PEND: hw.irq &= ~v; break;
    case R_TGY_CTRL:
        if (v & 8) hw.status &= ~0x16u;
        if (v & 4) hw.polls_left = 0;
        if ((v & 3) && (hw.polls_left > 0 || hw.force_busy || ((v & 1) && hw.reject_next))) { if ((v & 1) && hw.reject_next) hw.reject_next = 0; hw.status |= 0x10u; break; }      /* rejected: sticky until CLEAR, and START does not clear it (as tau_tgy_regs.sv) */
        if (v & 2) {
            if ((hw.qy | hw.qc) & 1u) { hw.bad_order++; break; }
            for (int i = 0; i < 64; i++) { hw.ty[i] = (uint16_t)(mem[hw.qy + 2 * i] | mem[hw.qy + 2 * i + 1] << 8); hw.tc[i] = (uint16_t)(mem[hw.qc + 2 * i] | mem[hw.qc + 2 * i + 1] << 8); }
            hw.tables_ok = 1; hw.loads++; hw.polls_left = 2; hw.status |= 2u;
        } else if (v & 1) {
            if (!hw.tables_ok) hw.bad_order++;
            if (hw.cfg_since_start < 7) hw.bad_order++;
            hw.cfg_since_start = 0; hw.starts++; run_tile(); hw.polls_left = 3; hw.irq |= 0x10u;
        }
        break;
    default: break;
    }
}
#include "../fw2/gallery/tgy_hw.c"
#include "../fw2/gallery/gallery_fmt.c"
static uint32_t card_base; static int rd_card(void *c, uint32_t off, uint32_t len, uint8_t *dst) { (void)c; memcpy(dst, mem + card_base + off, len); return 1; }
static int ticks; static int tick(void *c, uint32_t tx, uint32_t ty) { (void)c; (void)tx; (void)ty; ticks++; return 0; }
static uint32_t fetch_calls, fetch_fail_at;
static int fetch_entry(void *c, uint32_t off, uint8_t *out) { (void)c; fetch_calls++; if (fetch_fail_at && fetch_calls == fetch_fail_at) return 0; memcpy(out, mem + card_base + off, 12); return 1; }
static int rd_fail(void *c, uint32_t off, uint32_t len, uint8_t *dst) { (void)c; (void)off; (void)len; (void)dst; return 0; }
int main(int argc, char **argv)
{
    if (argc < 5) return 2; const char *mode = argv[1]; FILE *f = fopen(argv[2], "rb"); if (!f) return 2; uint32_t base = (uint32_t)strtoul(argv[3], 0, 0);
    mem = calloc(MEM_SIZE, 1); if (!mem) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (fread(mem + base, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    card_base = base; hw.present = strcmp(mode, "absent") != 0; hw.force_busy = !strcmp(mode, "busy"); const char *fm = (!strcmp(mode, "showfull") || !strcmp(mode, "sliced")) ? "full" : mode;
    if (!strcmp(mode, "badhdr")) mem[base + 9] ^= 0x40;
    if (!strcmp(mode, "prefix") && argc > 5) { n = strtol(argv[5], 0, 0); memset(mem + base + n, 0, (size_t)(MEM_SIZE - base - n)); }          /* only the first n bytes of the file are resident */
    tgy_file_t tf; int r = tgy_open(&tf, mem + base, (uint32_t)n, base);          /* the CPU's RAM copy of the header is the file's own first bytes here */
    printf("open %d\n", r); if (r != TH_OK) return 0;
    { tgy_level_t t; printf("full %d\n", tgy_find_level(&tf, 1u, &t)); }
    tgy_level_t l; r = tgy_find_level(&tf, !strcmp(fm, "full") ? 1u : 2u, &l); printf("find %d\n", r); if (r != TH_OK) return 0;
    printf("level %ux%u fmt %u tiles %ux%u rect %u,%u %ux%u\n", l.w, l.h, l.fmt, l.cols, l.rows, l.rx, l.ry, l.rw, l.rh);
    if (!strcmp(mode, "badidx")) mem[base + l.idx_off + 5] ^= 1;
    static uint8_t ibuf[65536];
    if (!strcmp(mode, "sliced")) {                                                  /* the index stays on the card: every entry through fetch equals the resident index, the checks around it, then the whole level decodes through it */
        tgy_level_t res = l; r = tgy_load_index(&tf, &res, rd_card, 0, ibuf, sizeof ibuf); printf("index %d\n", r); if (r != TH_OK) return 0;
        tgy_level_t sl = l; sl.idx = 0; sl.fetch = fetch_entry; uint32_t bad = 0, cnt = 0;
        for (uint32_t ty = 0; ty < l.rows; ty++) for (uint32_t tx = 0; tx < l.cols; tx++) { uint32_t o1 = 1, n1 = 1, o2 = 2, n2 = 2; const int ra = tgy_tile_loc(&res, tx, ty, &o1, &n1), rb = tgy_tile_loc(&sl, tx, ty, &o2, &n2); cnt++; if (ra != rb || (ra == TH_OK && (o1 != o2 || n1 != n2))) bad++; }
        printf("sliced same %u of %u calls %u\n", cnt - bad, cnt, fetch_calls);
        { uint32_t o, n2; tgy_level_t none = l; none.idx = 0; none.fetch = 0; printf("range %d %d %d noidx %d\n", tgy_tile_loc(&sl, l.cols, 0, &o, &n2), tgy_tile_loc(&sl, 0, l.rows, &o, &n2), tgy_tile_loc(&sl, 0xFFFFFFFFu, 0, &o, &n2), tgy_tile_loc(&none, 0, 0, &o, &n2)); }
        { uint32_t o, n2; fetch_fail_at = fetch_calls + 1; printf("fetchfail %d\n", tgy_tile_loc(&sl, 0, 0, &o, &n2)); fetch_fail_at = 0; }
        { static uint8_t vb[4096]; printf("verify %d small %d", tgy_verify_index(&tf, &l, rd_card, 0, vb, sizeof vb), tgy_verify_index(&tf, &l, rd_card, 0, vb, 100u));
          printf(" readfail %d zerocap %d", tgy_verify_index(&tf, &l, rd_fail, 0, vb, sizeof vb), tgy_verify_index(&tf, &l, rd_card, 0, vb, 0u));
          mem[base + l.idx_off + l.idx_len - 1u] ^= 0x80; printf(" lastbyte %d", tgy_verify_index(&tf, &l, rd_card, 0, vb, sizeof vb)); mem[base + l.idx_off + l.idx_len - 1u] ^= 0x80;
          mem[base + l.idx_off + 7u] ^= 1; printf(" firstbyte %d", tgy_verify_index(&tf, &l, rd_card, 0, vb, 4096u)); mem[base + l.idx_off + 7u] ^= 1; printf(" again %d\n", tgy_verify_index(&tf, &l, rd_card, 0, vb, sizeof vb)); }
        l = sl; fetch_calls = 0;
    } else {
    r = tgy_load_index(&tf, &l, rd_card, 0, ibuf, sizeof ibuf); printf("index %d\n", r); if (r != TH_OK) return 0; }
    if (!strcmp(mode, "hwerr")) { uint32_t ent = base + l.idx_off, off = mem[ent] | mem[ent + 1] << 8 | mem[ent + 2] << 16 | (uint32_t)mem[ent + 3] << 24; memset(mem + base + l.data_off + off, 0xFF, 40); }
    const uint32_t plane = 0x600000u, pitch_full = l.w * 3u + 5u, dst0 = 0x1000000u;
    if (!strcmp(mode, "show")) { memset(mem + plane, 0x55, 2400u * 720u); r = tgy_hw_show(&tf, &l, plane, tick, 0); }
    else if (!strcmp(mode, "full") || !strcmp(mode, "sliced")) { memset(mem + dst0, 0x55, (size_t)(pitch_full) * (l.h + 1u)); r = tgy_hw_level(&tf, &l, dst0 + 1u, pitch_full, tick, 0); }
    else if (!strcmp(mode, "odd")) { tgy_file_t t2 = tf; t2.base |= 1u; r = tgy_hw_tables(&t2, &l); }
    else if (!strcmp(mode, "both")) {                                               /* the fit level on the canvas, then the full level of the SAME file (different tables): the cache must reload */
        tgy_level_t lf; static uint8_t ibuf2[65536]; memset(mem + plane, 0x55, 2400u * 720u); r = tgy_hw_show(&tf, &l, plane, tick, 0);
        if (r == TH_OK && tgy_find_level(&tf, 1u, &lf) == TH_OK && tgy_load_index(&tf, &lf, rd_card, 0, ibuf2, sizeof ibuf2) == TH_OK) { memset(mem + dst0, 0x55, (size_t)(lf.w * 3u + 5u) * (lf.h + 1u)); r = tgy_hw_level(&tf, &lf, dst0 + 1u, lf.w * 3u + 5u, tick, 0); l = lf; }
    }
    else if (!strcmp(mode, "showfull")) { r = tgy_hw_show(&tf, &l, plane, tick, 0); }                                        /* a level wider than the canvas must be refused */
    else if (!strcmp(mode, "reloc")) {                                              /* every tile of the fit level decoded from a copy of its stream at an ODD address of its own (the zoom's staging), the original stream wiped */
        memset(mem + plane, 0x55, 2400u * 720u); r = TH_OK;
        for (uint32_t ty = 0; ty < l.rows && r == TH_OK; ty++) for (uint32_t tx = 0; tx < l.cols && r == TH_OK; tx++) {
            uint32_t fo, len; if (tgy_tile_loc(&l, tx, ty, &fo, &len) != TH_OK) { r = TH_RANGE; break; }
            const uint32_t src = 0x800000u + 1u + (ty * l.cols + tx) * 0x10000u; memcpy(mem + src, mem + base + fo, len); memset(mem + base + fo, 0xFF, len);
            r = tgy_hw_tile_from(&tf, &l, tx, ty, plane + (uint32_t)l.ry * GT_STRIDE + (uint32_t)l.rx * 3u + ty * TH_TILE * GT_STRIDE + tx * TH_TILE * 3u, GT_STRIDE, src);
        }
    }
    else if (!strcmp(mode, "retry")) {                                              /* the first start is rejected (a race with a busy decoder), the caller retries the same tile */
        hw.reject_next = 1; memset(mem + plane, 0x55, 2400u * 720u); int r1 = tgy_hw_tile(&tf, &l, 0, 0, plane + (uint32_t)l.ry * GT_STRIDE + (uint32_t)l.rx * 3u, GT_STRIDE);
        printf("first %d\n", r1); r = tgy_hw_tile(&tf, &l, 0, 0, plane + (uint32_t)l.ry * GT_STRIDE + (uint32_t)l.rx * 3u, GT_STRIDE);
    }
    else { memset(mem + plane, 0x55, 2400u * 720u); r = tgy_hw_show(&tf, &l, plane, tick, 0); }   /* show, two (first picture), badidx.. */
    if (!strcmp(mode, "two") && argc > 5) {                                              /* a second picture at the same base: reopen, show again */
        FILE *g = fopen(argv[5], "rb"); fseek(g, 0, SEEK_END); long n2 = ftell(g); fseek(g, 0, SEEK_SET); memset(mem + base, 0, (size_t)n); if (fread(mem + base, 1, (size_t)n2, g) != (size_t)n2) return 2; fclose(g);
        r = tgy_open(&tf, mem + base, (uint32_t)n2, base); if (r != TH_OK) { printf("open2 %d\n", r); return 0; }
        r = tgy_find_level(&tf, 2u, &l); if (r != TH_OK) { printf("find2 %d\n", r); return 0; }
        r = tgy_load_index(&tf, &l, rd_card, 0, ibuf, sizeof ibuf); if (r != TH_OK) { printf("index2 %d\n", r); return 0; }
        memset(mem + plane, 0x55, 2400u * 720u); r = tgy_hw_show(&tf, &l, plane, tick, 0);
    }
    printf("run %d status %08x ticks %d starts %d loads %d order %d\n", r, tgy_hw_status(), ticks, hw.starts, hw.loads, hw.bad_order);
    FILE *o = fopen(argv[4], "wb");
    if (!strcmp(mode, "show") || !strcmp(mode, "two") || !strcmp(mode, "retry") || !strcmp(mode, "prefix") || !strcmp(mode, "reloc")) fwrite(mem + plane, 1, 2400u * 720u, o); else if (!strcmp(mode, "full") || !strcmp(mode, "sliced")) fwrite(mem + dst0, 1, (size_t)pitch_full * (l.h + 1u), o);
    else if (!strcmp(mode, "both")) { fwrite(mem + plane, 1, 2400u * 720u, o); fwrite(mem + dst0, 1, (size_t)(l.w * 3u + 5u) * (l.h + 1u), o); }
    fclose(o); return 0;
}
