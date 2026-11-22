/* Host test of the Tau Gallery M1 firmware (fw2/gallery/gallery_app.c plus gallery_fmt.c and gallery_hw.c) against a model of the hardware that serves the REAL files the Python converter wrote:
 * the data slots read from the files, the mailbox writes a framebuffer, the landing path writes the SDRAM plane, the viewer mode sequencer, the keys come from a script. Checkpoints in the script compare the
 * framebuffer thumbnails with the atlas pixel for pixel, the plane with the picture's canvas byte for byte, the viewer state and the text of the status line.
 *   test_gallery_app DIR     (DIR holds tau-library.tdb and p00.tgy .. pNN.tgy, written by sim/test_gallery_app.py)
 * Prints "GALLERY APP ok n" / "GALLERY APP FAIL n"; exit 1 on a failure. Mutants are made by the Python driver (it edits copies of the firmware sources). */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#define P2_HOST 1
#define HW_N (16u * 1024u * 1024u)
#define T_WIN0 0x900000u                                       /* the resident windows and the staging area of the firmware (asserted equal to its own constants below) */
#define T_WIN1 0xB94000u
#define T_STAGE 0xE28000u
static uint16_t mem[HW_N];                         /* SDRAM, halfwords: the plane at 0x200000 */
static uint16_t fb[512u * 1024u];                  /* the UI framebuffer, halfwords, stride 512 */
static uint32_t hist_boost = 1u;                                                                                 /* every bin the histogram unit reports times this (a scenario sets it so the counts pass 65,535 and the page must scale them)  */
static uint32_t z2_vis_hits;                                                                                     /* decoder writes into the rows the screen is reading while the 2x loop runs (there must be none: only tiles outside the shown view are ever decoded) */
static uint32_t vlog[256], vlog_n, vsnap[256], vsnap_n;                                                          /* every write of the view base since the last checkpoint (the snapshot is what the checkpoint sees) */
static uint32_t clk, irq_pend, vid_req, vid_bus, vid_sw, vid_due, vbase, vstride, land_base, land_words, land_ovf, input_n;
static int force_ovf, force_short, viewer_dead, fail_pic_reads, pass_pic_reads; static uint32_t v1_starts_before, land_unaligned, zt_n_212, rwd_241;
static int zb_ok_phase = 0; static int32_t map_cx, map_cy, map_rx1, map_rw2, map_x0, map_y0, map_rx0, map_ry0, map_ex, map_ey, map_rw1;
static uint32_t rc_lead_n, rc_lead_bad; static struct { uint32_t src, len, dst, pitch, geom, qy, qc, status, polls, cycles; uint16_t ty[64], tc[64]; int tables_ok; uint32_t starts, loads; int corrupt_next; int pending; uint32_t p_src, p_len, p_dst, p_pitch, p_geom; int32_t stx, sty, p_stx, p_sty; } tg;
static uint32_t reads_while_decoding;
static uint32_t rc_prev, rc_starts, rc_retarget; static int32_t rc_bx, rc_by;
static void ldr_lead_probe(void);
static void ldr_lead_probe(void);
static void ldr_probe(uint32_t dst);
static void tile_read_probe(uint32_t off);
static void zb_probe(void);
static void pass_hook(void);                                                                  /* called once per pass of the firmware's loop (a read of the keys): defined after the firmware, which it looks into */
static uint32_t blits_since_input, blit_max_pass; static uint32_t blit_cmds, blit_visible, blit_bad, blit_from_win, tile_reads, win_flips, last_win = 9u; static uint32_t fail_idx, blit_cost, rs_f0, rs_c0, rs_catch0, rs_moved0, rs_sel0; static int32_t rs_wx0; static uint32_t poll_alt, poll_adv, clk_step = 50u, zoom_max_len, zoom_max_rs, reads_since_input; static int zoom_phase;      /* the zoom scenario: a frame a clock read, the longest card command and the most commands between two key polls */ static uint32_t text_mode;
static uint32_t setw[16], setidx; static int set_writes;                  /* the settings register file (R_SET_IDX / R_SET_DAT) and how many words were written */
static void tick(void) { if (!viewer_dead && vid_req != vid_bus && vid_due && clk >= vid_due) { vid_bus = vid_req; vid_sw++; vid_due = 0; } }
static uint32_t next_input(void);
static uint32_t tgy_rd(uint32_t a); static void tgy_wr(uint32_t a, uint32_t v); static int tgy_present = 1;       /* the model of the hardware TGY decoder (registers 0x220-0x244), after the firmware sources */
static uint32_t caps_val = 0x80000003u;                                                                            /* VIEW_CAPS: bit 2 = the core has the pixel effects and the histogram unit */
static uint32_t hs_en, hs_rect0, hs_rect1, hs_org, hs_data, hs_n, hs_cnt, hs_clr_busy, hs_rd_busy, hs_bin[4][256], hs_starts;           /* the model of the histogram unit (tau_hist_if.sv): a clear and a read are busy for a few polls */
static void hs_count(unsigned row, const uint8_t *rgb, uint32_t w)                                                 /* called for every decoded row of a tile: the window test and the bins, written from the register description */
{
    if (!(hs_en & 1u) || hs_clr_busy) return;
    const uint32_t x0 = hs_rect0 & 1023u, y0 = (hs_rect0 >> 16) & 1023u, x1 = hs_rect1 & 1023u, y1 = (hs_rect1 >> 16) & 1023u, ox = hs_org & 1023u, oy = (hs_org >> 16) & 1023u;
    for (uint32_t x = 0; x < w; x++) {
        const uint32_t cx = ox + x, cy = oy + row; if (cx < x0 || cx >= x1 || cy < y0 || cy >= y1) continue;
        const uint32_t r = rgb[3u * x], g = rgb[3u * x + 1u], b = rgb[3u * x + 2u];
        hs_bin[0][(77u * r + 150u * g + 29u * b) >> 8]++; hs_bin[1][r]++; hs_bin[2][g]++; hs_bin[3][b]++; hs_cnt++;
    }
}
static uint32_t pix_ctrl, pix_lut[3][256], pix_stores, pix_busy, pix_bad;                              /* the model of the pixel effects: control word, tables, stores, a busy count after each store, stores made while busy or while the tables are on */
static int pix_tables_are(uint32_t p);
static uint32_t rd(uint32_t a)
{
    if (a == 0x80000260u) { const uint32_t v = (hs_clr_busy ? 0x100u : 0u) | (hs_en & 1u); if (hs_clr_busy) hs_clr_busy--; return v; }
    if (a == 0x80000274u) { const uint32_t v = hs_rd_busy ? 0x80000000u : hs_data; if (hs_rd_busy) hs_rd_busy--; return v; }
    if (a == 0x80000278u) return hs_n;
    if (a == 0x80000250u) { if (pix_busy) pix_busy--; return (pix_ctrl & 15u) | (pix_busy ? 0x100u : 0u); }
    if (a >= 0x80000220u && a <= 0x8000024Cu) return tgy_rd(a);
    switch (a) {
    case 0x80000084u: return 0;
    case 0x800001ECu: return (vid_sw << 16) | (vid_req << 2) | vid_bus;
    case 0x800001F0u: return vbase;
    case 0x800001F4u: return vstride;
    case 0x80000208u: return 0x80000001u;
    case 0x8000020Cu: return land_words;
    case 0x80000210u: return force_ovf ? 1u : land_ovf;
    case 0x80000040u: return 0x4D50331Bu;
    case 0x800001F8u: return caps_val;
    case 0x80000070u: return setw[setidx & 15u];
    case 0x80000114u: return 0x80000000u | text_mode;
    case 0x8000003Cu: reads_since_input = 0; blits_since_input = 0; pass_hook(); { const uint32_t k = next_input(); clk += (poll_alt && (input_n & 1u)) ? poll_alt : poll_adv; return k; }       /* in the zoom scenarios a key poll is a frame (the checkpoints that set poll_adv are handled first) */
    default: return 0;
    }
}
static uint32_t cyc_rd(void) { clk += clk_step; tick(); return clk; }
static void wr(uint32_t a, uint32_t v)
{
    if (a >= 0x80000220u && a <= 0x8000024Cu) { tgy_wr(a, v); return; }
    switch (a) {
    case 0x80000114u: text_mode = v & 1u; break;
    case 0x8000006Cu: setidx = v; break;
    case 0x80000070u: setw[setidx & 15u] = v; set_writes++; break;
    case 0x80000250u: pix_ctrl = v & 15u; break;
    case 0x80000260u: hs_en = v & 1u; if (v & 2u) { memset(hs_bin, 0, sizeof hs_bin); hs_cnt = 0u; hs_clr_busy = 3u; } break;
    case 0x80000264u: hs_rect0 = v; break; case 0x80000268u: hs_rect1 = v; break; case 0x8000026Cu: hs_org = v; hs_starts++; break;
    case 0x80000270u: hs_data = (hs_bin[(v >> 8) & 3u][v & 255u] * hist_boost) & 0xFFFFFFu; hs_n = hs_cnt; hs_rd_busy = 2u; break;
    case 0x80000254u: { const uint32_t ch = (v >> 16) & 3u; if (pix_busy || (pix_ctrl & 4u) || ch > 2u) pix_bad++; else { pix_lut[ch][(v >> 8) & 255u] = v & 255u; pix_stores++; pix_busy = 3u; } } break;
    case 0x800001ECu: vid_req = v & 1u; vid_due = clk + 2u * 1111111u; break;
    case 0x800001F0u: vbase = v & 0x1FFFFFFu; if (vlog_n < 256u) vlog[vlog_n++] = vbase; { const uint32_t wn = (vbase >= T_WIN1 && vbase < T_STAGE) ? 1u : (vbase >= T_WIN0 && vbase < T_WIN1) ? 0u : 9u; if (wn != 9u && wn != last_win) { if (last_win != 9u) win_flips++; last_win = wn; } } break;
    case 0x800001F4u: vstride = v & 0xFFFFu; break;
    case 0x80000200u: land_base = v & 0x1FFFFFFu; break;
    case 0x80000204u: if (v & 1u) { land_words = 0; land_ovf = 0; } break;
    default: break;
    }
}
#define REG_RD(a) ((a) == 0x8000000Cu ? cyc_rd() : rd(a))
#define REG_WR(a, v) wr((a), (v))
#include "../fw2/platform/mmio.h"
#include "../fw2/gfx/gfx.h"
#include "../fw2/stream/slot.h"
#include "../fw2/gallery/gallery_lut.h"
static int pix_tables_are(uint32_t p) { for (uint32_t c = 0; c < 3u; c++) for (uint32_t i = 0; i < 256u; i++) if (pix_lut[c][i] != gl_value(p, c, i)) return 0; return 1; }

/* ---- the files ---- */
static uint8_t *idx_data, *pic_data; static uint32_t idx_len, pic_len; static char dir[512]; static char last_open[200]; static int hidden = -1, reads, opens;
static uint8_t *slurp(const char *p, uint32_t *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)l); if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(2); fclose(f); *n = (uint32_t)l; return b; }
int slot_open_path(uint32_t slot, const char *path)
{
    opens++; strncpy(last_open, path, sizeof last_open - 1);
    if (slot != 2u || path[0] != '/') return 0;
    const char *b = strrchr(path, '/') + 1; char f[600]; snprintf(f, sizeof f, "%s/%s", dir, b);
    if (hidden >= 0) { char h[16]; snprintf(h, sizeof h, "p%02d.tgy", hidden); if (!strcmp(b, h)) return 0; }
    free(pic_data); pic_data = slurp(f, &pic_len); return pic_data != 0;
}
uint32_t _tag_start[1024];
static uint32_t rc_prev, rc_starts, rc_retarget; static int32_t rc_bx, rc_by;
static uint32_t ldr_pct, ldr_chunks, ldr_max, ldr_prev, ldr_first_ok = 1u, ldr_mono = 1u, ldr_skip, ldr_opens;
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)to; reads++;
    const int tagwin = dst == (uint32_t)(uintptr_t)_tag_start;                                   /* the tag window's own address, cut to 32 bits like the firmware passes it: on a 64 bit host with address randomisation its top nibble can be 4, which is the landing window's (a flaky failure on Linux) */
    if (zoom_phase && slot == 2u && len == 49152u) { tile_reads++; tile_read_probe(off); }
    if (!zoom_phase && slot == 2u && len == 131072u && !tagwin && dst >= 0x40000000u) ldr_probe(dst);
    if (zoom_phase && slot == 2u) { if (len > zoom_max_len) zoom_max_len = len; if (++reads_since_input > zoom_max_rs) zoom_max_rs = reads_since_input; }
    if (tg.pending) reads_while_decoding++;                                                      /* a card read while the decoder runs: the pipeline overlaps them */
    if (slot == 2u && !tagwin && (dst >> 28) == 4u && ((off & 4095u) || (len & 4095u))) land_unaligned++;      /* a landing read of a picture starts and ends on 4 KB (the APF reads fastest aligned; every landing in this firmware keeps to it) */
    if (slot == 5u && fail_idx) return 0;                                                         /* the index cannot be read (the thumbnail of the details page) */
    if (slot == 2u) { if (pass_pic_reads > 0) pass_pic_reads--; else if (fail_pic_reads > 0) { fail_pic_reads--; return 0; } }      /* a transient card error: after `pass` good reads the next `fail` reads of a picture fail */
    const uint8_t *src = slot == 5u ? idx_data : slot == 2u ? pic_data : 0; const uint32_t n = slot == 5u ? idx_len : pic_len;
    if (!src || off + len > n || (len & 3u)) return 0;                                        /* a read past the end of the file fails, as on the Pocket */
    if (!tagwin && (dst >> 28) == 4u) {
        for (uint32_t i = 0; i < len; i++) { const uint32_t pos = (dst & 0x0FFFFFFFu) + i, h = land_base + pos / 2u, b = pos & 1u; mem[h] = (uint16_t)((mem[h] & ~(0xFFu << (8u * b))) | (uint32_t)src[off + i] << (8u * b)); }
        land_words += len / 4u - (force_short ? 1u : 0u);
    } else { if (len > 4096u) return 0; memcpy(_tag_start, src + off, len); }
    clk += len * 78u; return 1;
}
#define GAL_TAGWIN ((const volatile uint32_t *)_tag_start)
int gfx_mb_write(uint32_t a, uint32_t d) { if (a + 1u >= HW_N) return -1; mem[a] = (uint16_t)d; mem[a + 1u] = (uint16_t)(d >> 16); return 0; }
int gfx_mb_read(uint32_t a, uint32_t *o) { *o = (uint32_t)mem[a] | (uint32_t)mem[a + 1u] << 16; return 0; }
int gfx_mb_write_buf(uint32_t a, uint32_t d) { if (a + 1u >= sizeof fb / 2u) return -1; fb[a] = (uint16_t)d; fb[a + 1u] = (uint16_t)(d >> 16); return 0; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = y; j < y + h && j < 1024u; j++) for (uint32_t i = x; i < x + w && i < 512u; i++) fb[j * 512u + i] = c; }
static char texts[64][64]; static uint32_t texts_y[64]; static int ntext;
/* Talos ops of core rev 30, modelled: OP_CORNERS paints the cut of each corner (a circle of radius r) in bg; OP_DSBLIT scales DOWN from the stash, every output pixel the rounded average of a 2 x 2 source box at floor(i * den / num). */
static uint32_t corner_ops, dsblit_ops, dsblit_bad;                                                             /* dsblit_bad: a scale whose source is not in the shared stash rows (360 and below): the second UI buffer would read another picture */
void gfx_corners(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint16_t bg)
{
    corner_ops++;
    if (r > 15u) r = 15u;
    for (uint32_t dy = 0; dy < r && dy < h; dy++) {
        const double dd = (double)r - (double)dy - 0.5; const uint32_t in = (uint32_t)floor(sqrt((double)r * r - dd * dd) + 0.5), cut = r > in ? r - in : 0u;
        for (uint32_t k = 0; k < cut && k < w; k++) { fb[(y + dy) * 512u + x + k] = bg; fb[(y + dy) * 512u + x + w - 1u - k] = bg; fb[(y + h - 1u - dy) * 512u + x + k] = bg; fb[(y + h - 1u - dy) * 512u + x + w - 1u - k] = bg; }
    }
}
void gfx_dsblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry)
{
    static const uint32_t NUM[4] = { 3, 2, 1, 1 }, DEN[4] = { 4, 3, 2, 3 };
    dsblit_ops++; if (sy < 360u) dsblit_bad++;
    const uint32_t nx = NUM[rx & 3u], dxn = DEN[rx & 3u], ny = NUM[ry & 3u], dyn = DEN[ry & 3u], ow = w * nx / dxn, oh = h * ny / dyn;
    for (uint32_t j = 0; j < oh; j++) for (uint32_t i = 0; i < ow; i++) {
        const uint32_t x0 = i * dxn / nx, y0 = j * dyn / ny; uint32_t r5 = 0, g6 = 0, b5 = 0;
        for (uint32_t t = 0; t < 4u; t++) { const uint16_t p = fb[(sy + y0 + (t >> 1)) * 512u + sx + x0 + (t & 1u)]; r5 += p >> 11; g6 += (p >> 5) & 63u; b5 += p & 31u; }
        fb[(dy + j) * 512u + dx + i] = (uint16_t)((((r5 + 2u) >> 2) << 11) | (((g6 + 2u) >> 2) << 5) | ((b5 + 2u) >> 2));
    }
}
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { (void)x; (void)fg; (void)bg; if (ntext == 64) ntext = 0; texts_y[ntext] = y; strncpy(texts[ntext], s, 63); texts[ntext++][63] = 0; return 0; }
uint32_t gfx_text_width(const char *s) { return 8u * (uint32_t)strlen(s); }
void irq_enable(uint32_t m) { (void)m; }
/* the Talos BLIT as the RTL has it (docs/M2_DESIGN.md section 9, sim/tb_talos_tilecopy.v): bases 25 bits, offsets 19 bits, a row at most 127 halfwords; this model also counts copies that read a window and any write into the displayed screen area */
void gfx_fence(void) { }
void gfx_blit_raw(uint32_t sb, uint32_t db, uint32_t so, uint32_t dof, uint32_t w)
{
    if (!w || w > 127u || so >= (1u << 19) || dof >= (1u << 19) || sb >= (1u << 25) || db >= (1u << 25)) { blit_bad++; return; }
    const uint32_t s = sb + so, d = db + dof; blit_cmds++; clk += blit_cost; if (++blits_since_input > blit_max_pass) blit_max_pass = blits_since_input;
    if (s >= T_WIN0 && s < T_STAGE) blit_from_win++;
    if (vstride == 2112u && vbase >= T_WIN0 && vbase < T_STAGE && d >= vbase) {                  /* the visible rows of the window being displayed: 720 lines of 1,200 halfwords */
        const uint32_t rel = d - vbase, row = rel / vstride, col = rel % vstride;
        if ((row < 720u && col < 1200u) || (col + w > vstride && row + 1u < 720u)) blit_visible++;
    }
    if (d + w > HW_N || s + w > HW_N) { blit_bad++; return; }
    for (uint32_t i = 0; i < w; i++) mem[d + i] = mem[s + i];
}
void irq_disable(void) { }
int platform_version_ok(void) { return 1; }
void platform_halt_pattern(void) { exit(3); }
void platform_probe(void) { }

/* ---- the key script ---- */
#define CP(n) (0x10000u | (n))
#define END 0x20000u
static uint32_t script[32768]; static int nscript;
static jmp_buf done;
static int fails;
static void chk(int n, int ok) { printf("GALLERY APP %s %d\n", ok ? "ok" : "FAIL", n); if (!ok) fails++; }
static void checkpoint(int n);
static uint32_t last_key;
static int zk; static uint32_t z2_active, map_active;                                                                          /* the firmware's own (defined below): tentative here for next_input */
static uint32_t legacy_a = 1u, inj_release, inj_active;                                                            /* the scenarios written before 2019-11-19 press A in a zoom to go back to the fit view; A goes the next level IN now and B back one. With legacy_a the harness turns such an A or B into B presses (as many as the zoom has levels) and drops it */
static uint32_t next_input(void)
{
    for (;;) {
        if (inj_release) { inj_release = 0u; return 0u; }
        if (input_n >= (uint32_t)nscript) longjmp(done, 1);
        const uint32_t t = script[input_n];
        if (t == END) longjmp(done, 1);
        if (legacy_a && (t == KEY_A || t == KEY_B) && !z2_active && !map_active) {                                    /* (B went to the fit view from any level too) */
            if (zk >= 0) { inj_active = 1u; inj_release = 1u; last_key = KEY_B; return KEY_B; }       /* still zoomed: B, then the release, and look again */
            if (inj_active) { inj_active = 0u; input_n++; continue; }                                  /* back at the fit view: that A is spent */
        }
        input_n++;
        if (t & 0x10000u) { checkpoint((int)(t & 0xFFFFu)); continue; }
        last_key = t;
        return t;
    }
}
#define main gallery_app_main
#include "../fw2/gallery/gallery_fmt.c"
#include "../fw2/gallery/tgy_hw.c"
#include "../fw2/gallery/tgy_dec.c"
#include "../fw2/gallery/tgy_pix.c"
#include "../fw2/gallery/gallery_pattern.c"
#include "../fw2/gallery/gallery_hw.c"
#include "../fw2/gallery/gallery_settings.c"
#include "../fw2/gallery/gallery_theme_stub.c"
#include "../fw2/ui/theme.c"
#include "../fw2/gallery/gallery_app.c"
#undef main

/* ---- the hardware TGY decoder as tau_tgy_regs.sv / tau_tgy_dma.sv define it, over the SDRAM model: LOAD_TABLES reads the two tables at the programmed byte addresses, START decodes the tile at SRC with the C reference
 * pipeline and writes its RGB rows at DST + row * PITCH byte for byte; busy for a few polls; done / err / rejected like the RTL. ---- */
static tgy_dec_t tg_dec; static tgy_planes_t tg_planes;
static uint8_t mbyte(uint32_t a) { return (uint8_t)(mem[a / 2u] >> (8u * (a & 1u))); }
static void msetb(uint32_t a, uint8_t v) { const uint32_t h = a / 2u, b = a & 1u; mem[h] = (uint16_t)((mem[h] & ~(0xFFu << (8u * b))) | (uint32_t)v << (8u * b)); }
typedef struct { uint32_t dst, pitch, w, scat; int32_t stx, sty; } tg_emit_t;
static void tg_emit(void *c, unsigned row, const uint8_t *rgb)
{
    tg_emit_t *e = c;
    if (e->scat && z2_active && vstride) for (uint32_t x = 0; x < e->w; x++) { const uint32_t hw = (uint32_t)((int64_t)e->dst + (int64_t)x * e->stx + (int64_t)row * e->sty) / 2u; if (hw >= vbase && (hw - vbase) / vstride < (uint32_t)z2_vh && (hw - vbase) % vstride < (uint32_t)z2_vw * 3u / 2u) z2_vis_hits++; }      /* a 2x pixel written where the screen is reading */
    if (e->scat) { for (uint32_t x = 0; x < e->w; x++) for (uint32_t k = 0; k < 3u; k++) msetb((uint32_t)((int64_t)e->dst + (int64_t)x * e->stx + (int64_t)row * e->sty) + k, rgb[3u * x + k]); return; }        /* K7: every pixel at dst + x * stx + y * sty */
    for (uint32_t i = 0; i < e->w * 3u; i++) msetb(e->dst + row * e->pitch + i, rgb[i]);
}
static void tg_emit_h(void *c, unsigned row, const uint8_t *rgb) { hs_count(row, rgb, ((tg_emit_t *)c)->w); tg_emit(c, row, rgb); }
static void tg_complete(void)                                                                                      /* the decoder finishes: fetch the stream from memory as it is NOW, decode, write the rows, set done or err */
{
    const unsigned fmt = (tg.p_geom >> 16) & 3u, w = (tg.p_geom >> 8) & 255u, h = tg.p_geom & 255u; uint8_t rows[3] = {0}, cols[3] = {0}; static uint8_t sbuf[1u << 20];
    if (fmt == 2u) { rows[0] = (uint8_t)(2u * ((h + 15u) / 16u)); cols[0] = (uint8_t)(2u * ((w + 15u) / 16u)); rows[1] = rows[2] = (uint8_t)((h + 15u) / 16u); cols[1] = cols[2] = (uint8_t)((w + 15u) / 16u); }
    else { rows[0] = rows[1] = rows[2] = (uint8_t)((h + 7u) / 8u); cols[0] = cols[1] = cols[2] = (uint8_t)((w + 7u) / 8u); }
    for (uint32_t i = 0; i < tg.p_len && i < sizeof sbuf; i++) sbuf[i] = mbyte(tg.p_src + i);
    tg_emit_t e = { tg.p_dst, tg.p_pitch, w, (tg.p_geom >> 18) & 1u, tg.p_stx, tg.p_sty };
    const int err = (fmt == 1u || !w || !h || w > 128u || h > 128u) ? 1 : tgy_tile_rgb(&tg_dec, &tg_planes, sbuf, tg.p_len, fmt, rows, cols, w, h, tg.ty, tg.tc, tg_emit_h, &e);
    tg.status = (tg.status & ~0x6u) | (err ? 4u : 2u) | (err ? (1u << 8) : 0u); tg.pending = 0; tg.cycles = 62u * tg.p_len + 20000u;                          /* the DMA budget of the RTL test: 62 SDRAM clocks a byte */
}
static uint32_t tgy_rd(uint32_t a)
{
    if (!tgy_present) return 0u;
    switch (a) {
    case 0x80000224u: { uint32_t s = tg.status & ~1u; if (tg.polls > 0) { tg.polls--; s |= 1u; if (tg.polls == 0 && tg.pending) tg_complete(); } return 0x80000000u | s | (tg.tables_ok ? 8u : 0u); }
    case 0x80000244u: return tg.cycles;
    default: return 0u;
    }
}
static void tgy_wr(uint32_t a, uint32_t v)
{
    if (!tgy_present) return;
    switch (a) {
    case 0x80000228u: tg.src = v & 0x3FFFFFFu; break; case 0x8000022Cu: tg.len = v & 0xFFFFFu; break; case 0x80000230u: tg.dst = v & 0x3FFFFFFu; break; case 0x80000234u: tg.pitch = v & 0xFFFFu; break;
    case 0x80000238u: tg.geom = v & 0x7FFFFu; break; case 0x80000248u: tg.stx = (int32_t)(v << 14) >> 14; break; case 0x8000024Cu: tg.sty = (int32_t)(v << 14) >> 14; break; case 0x8000023Cu: tg.qy = v & 0x3FFFFFFu; break; case 0x80000240u: tg.qc = v & 0x3FFFFFFu; break;
    case 0x80000220u:
        if (v & 8u) tg.status &= ~0x16u;
        if ((v & 3u) && tg.polls > 0) { tg.status |= 0x10u; break; }
        if (v & 2u) {
            for (int i = 0; i < 64; i++) { tg.ty[i] = (uint16_t)(mbyte(tg.qy + 2u * i) | mbyte(tg.qy + 2u * i + 1u) << 8); tg.tc[i] = (uint16_t)(mbyte(tg.qc + 2u * i) | mbyte(tg.qc + 2u * i + 1u) << 8); }
            tg.tables_ok = 1; tg.loads++; tg.polls = 2; tg.status |= 2u;
        } else if (v & 1u) {
            const unsigned fmt = (tg.geom >> 16) & 3u, w = (tg.geom >> 8) & 255u, h = tg.geom & 255u; uint8_t rows[3] = {0}, cols[3] = {0}; static uint8_t sbuf[1u << 20];
            if (fmt == 2u) { rows[0] = (uint8_t)(2u * ((h + 15u) / 16u)); cols[0] = (uint8_t)(2u * ((w + 15u) / 16u)); rows[1] = rows[2] = (uint8_t)((h + 15u) / 16u); cols[1] = cols[2] = (uint8_t)((w + 15u) / 16u); }
            else { rows[0] = rows[1] = rows[2] = (uint8_t)((h + 7u) / 8u); cols[0] = cols[1] = cols[2] = (uint8_t)((w + 7u) / 8u); }
            (void)rows; (void)cols; (void)sbuf; (void)w; (void)h; (void)fmt;
            tg.pending = 1; tg.p_src = tg.src; tg.p_len = tg.len; tg.p_dst = tg.dst; tg.p_pitch = tg.pitch; tg.p_geom = tg.geom; tg.p_stx = tg.stx; tg.p_sty = tg.sty; tg.starts++; tg.polls = 3;       /* the decode happens when the busy polls run out: the stream is read THEN, so a stream that is overwritten while the decoder runs decodes wrong (as the DMA would read it) */
        }
        break;
    default: break;
    }
}
_Static_assert(T_WIN0 == GAL_WIN0 && T_WIN1 == GAL_WIN1 && T_STAGE == GAL_STAGE, "the model's window addresses are the firmware's");

static uint32_t count;
static const uint8_t *thumb_of(uint32_t i) { uint32_t atlas = (uint32_t)idx_data[20] | (uint32_t)idx_data[21] << 8 | (uint32_t)idx_data[22] << 16; return idx_data + atlas + i * 13824u; }
static int hist_screen_ok(uint32_t i)    /* the details page's four histograms against a count made here from the index's thumbnail (separate code, floating point for the luma) and the bars' pixels in the framebuffer */
{
    const uint8_t *t = thumb_of(i); uint32_t cnt[4][32]; memset(cnt, 0, sizeof cnt);
    /* the picture's own pixels of the thumbnail (not its black bars): the converter's fit rule in floating point, one pixel in from each edge that borders a bar */
    const uint32_t pw = EN(i).width, ph = EN(i).height;
    double sc = fmin(fmin(96.0 / pw, 72.0 / ph), 1.0); uint32_t rw = (uint32_t)nearbyint(pw * sc), rh = (uint32_t)nearbyint(ph * sc); if (rw < 1u) rw = 1u; if (rh < 1u) rh = 1u; if (rw > 96u) rw = 96u; if (rh > 72u) rh = 72u;
    uint32_t X0 = (96u - rw) / 2u, Y0 = (72u - rh) / 2u, X1 = X0 + rw, Y1 = Y0 + rh; if (rw < 96u && rw > 2u) { X0++; X1--; } if (rh < 72u && rh > 2u) { Y0++; Y1--; }
    uint32_t inside = 0u;
    for (uint32_t p = 0; p < 96u * 72u; p++) {
        if (p / 96u < Y0 || p / 96u >= Y1 || p % 96u < X0 || p % 96u >= X1) continue; inside++;
        const uint32_t v = (uint32_t)t[2u * p] | (uint32_t)t[2u * p + 1u] << 8; const double R = ((v >> 11) & 31u) * 255.0 / 31.0, G = ((v >> 5) & 63u) * 255.0 / 63.0, B = (v & 31u) * 255.0 / 31.0;
        uint32_t y = (uint32_t)((77.0 * R + 150.0 * G + 29.0 * B) / 256.0 / 8.0); if (y > 31u) y = 31u;
        cnt[0][y]++; cnt[1][(v >> 11) & 31u]++; cnt[2][((v >> 5) & 63u) / 2u]++; cnt[3][v & 31u]++;
    }
    int ok = det_hist.n == inside && inside > 0u && inside < 96u * 72u + 1u; const uint16_t col[4] = { 0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu };
    for (uint32_t k = 0; k < 4u; k++) {
        uint32_t max = 0u, diff = 0u; for (uint32_t b = 0; b < 32u; b++) { if (cnt[k][b] > max) max = cnt[k][b]; if (cnt[k][b] != det_hist.bin[k][b]) diff++; }
        if (diff > 40u) ok = 0;                                                                                        /* the luma bin may differ by one at a bin edge: a few pixels move to a neighbour, never many */
        const uint32_t fx0 = DET_PX, fy0 = DET_PY0 + k * DET_PSTEP;
        for (uint32_t b = 0; b < 32u; b++) {
            const uint32_t n = det_hist.bin[k][b], h = n ? ((uint32_t)(44.0 * floor(sqrt((double)(n << 8))) / floor(sqrt((double)(max << 8)))) < 1u ? 1u : (uint32_t)(44.0 * floor(sqrt((double)(n << 8))) / floor(sqrt((double)(max << 8))))) : 0u;
            const uint32_t px = fx0 + 3u + b * 8u + 1u, bottom = fy0 + 68u - 4u;                                       /* a thumbnail's bin is a bar 7 pixels wide in an 8 pixel slot, 44 pixels high at most, 4 above the frame's bottom */
            for (uint32_t r = 0; r < 45u; r++) { const uint32_t yy = bottom - 1u - r; const int on = fb[yy * 512u + px] == col[k]; if (on != (r < h)) ok = 0; }
            if (fb[(bottom - 1u) * 512u + fx0 + 3u + b * 8u + 7u] == col[k]) ok = 0;                                  /* the gap column between bars is clear */
        }
    }
    return ok;
}
static int hist_fine_screen_ok(void)                                                                                  /* a decoder histogram on the screen: one 1 pixel bar for each of the 256 levels, 3 pixels in from the frame, 44 pixels high at most (the tallest of its colour), 4 above the frame's bottom */
{
    const uint16_t col[4] = { 0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu }; int ok = det_hist.is_fine == 1u;
    for (uint32_t k = 0; k < 4u; k++) {
        uint32_t max = 0u; for (uint32_t i = 0; i < 256u; i++) if (det_hist.fine[k][i] > max) max = det_hist.fine[k][i];
        const uint32_t fx0 = DET_PX, fy0 = DET_PY0 + k * DET_PSTEP, bottom = fy0 + 68u - 4u;
        for (uint32_t i = 0; i < 256u; i++) {
            const uint32_t n = det_hist.fine[k][i], h = n ? ((uint32_t)(44.0 * floor(sqrt((double)(n << 8))) / floor(sqrt((double)(max << 8)))) < 1u ? 1u : (uint32_t)(44.0 * floor(sqrt((double)(n << 8))) / floor(sqrt((double)(max << 8))))) : 0u;
            for (uint32_t r = 0; r < 45u; r++) { const int on = fb[(bottom - 1u - r) * 512u + fx0 + 3u + i] == col[k]; if (on != (r < h)) ok = 0; }
            if (fb[(bottom - 1u) * 512u + fx0 + 3u + i + 1u] == col[k] && i == 255u) ok = 0;                          /* nothing right of the last level */
        }
    }
    return ok;
}
static int thumbs_on_screen(uint32_t first, uint32_t n)                                          /* every pixel of the page's thumbnails against the atlas */
{
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *t = thumb_of(first + k); const uint32_t x = 2u + (k % 4u) * 100u, y = 22u + (k / 4u) * 76u;
        for (uint32_t r = 0; r < 72u; r++) for (uint32_t c = 0; c < 96u; c++) { const uint32_t o = (r * 96u + c) * 2u; if (fb[(y + r) * 512u + x + c] != (uint16_t)(t[o] | t[o + 1] << 8)) return 0; }
        for (uint32_t c = 0; c < 96u; c++) { const uint16_t v = fb[(y + 72u) * 512u + x + c]; if (v != th_role[TR_BG_BOTTOM] && v != th_role[TR_ACCENT]) return 0; }          /* nothing written under the thumbnail: black or the selection frame */
    }
    return 1;
}

/* ---- the rotated view: the plane of the turned picture against the reference decode turned here (explicit formulas, not the firmware's matrix table) ---- */
static void ref_turn(uint32_t o, uint32_t iw, uint32_t ih, uint32_t x, uint32_t y, uint32_t *X, uint32_t *Y)
{
    switch (o) {
    case 0: *X = x; *Y = y; break;               case 1: *X = iw - 1u - x; *Y = y; break;
    case 2: *X = x; *Y = ih - 1u - y; break;     case 3: *X = iw - 1u - x; *Y = ih - 1u - y; break;
    case 4: *X = y; *Y = x; break;               case 5: *X = y; *Y = iw - 1u - x; break;
    case 6: *X = ih - 1u - y; *Y = x; break;     default: *X = ih - 1u - y; *Y = iw - 1u - x; break;
    }
}
static int lab_skip(uint32_t hw) { return lab_on && hw >= lab_base && hw < lab_base + GLB_H * GAL_LINE_WORDS16; }        /* the halfword is under the label's bar: the plane there is the label's, not the picture's (the bar's own pixels are checked on their own) */
static int label_ok(const char *text)                                                                               /* the label's bar on the plane, pixel for pixel: 32 rows, black, the text in white 16 pixels in and 5 rows down, and the rows above it are not touched (the picture checks skip only the bar) */
{
    if (!lab_on || lab_base != (rot_o ? GAL_ROT_BASE + rot_pan * GAL_LINE_WORDS16 : GAL_PLANE_BASE) + GLB_Y0 * GAL_LINE_WORDS16) return 0;      /* on the last 32 rows of what the screen shows (a turned picture's plane is panned) */
    for (uint32_t y = 0; y < GLB_H; y++) for (uint32_t x = 0; x < 800u; x++) for (uint32_t k = 0; k < 3u; k++) {
        const uint32_t b = 3u * x + k, got = (mem[lab_base + y * GAL_LINE_WORDS16 + b / 2u] >> (8u * (b & 1u))) & 255u;
        const int on = y >= GLB_TY && y < GLB_TY + 21u && x >= GLB_TX && x < GLB_PX && glb_on(text, x - GLB_TX, y - GLB_TY);
        if (got != (on ? 255u : 0u)) return 0;
    }
    return 1;
}
static int label_view_ok(const char *text)                                                                         /* the bar on a zoom plane: it lies on the last 32 rows of what the screen reads (from the view registers), black with the text in white, pixel for pixel */
{
    if (!lab_on || lab_stride != vstride || lab_base != vbase + lab_y0 * vstride) return 0;
    for (uint32_t y = 0; y < lab_h; y++) for (uint32_t x = 0; x < lab_w; x++) for (uint32_t k = 0; k < 3u; k++) {
        const uint32_t b = 3u * x + k, got = (mem[lab_base + y * lab_stride + b / 2u] >> (8u * (b & 1u))) & 255u;
        const int on = y >= lab_ty && y < lab_ty + 7u * lab_sc && x >= lab_tx && x < GLB_PX && glb_on_g(text, x - lab_tx, y - lab_ty, lab_sc);
        if (got != (on ? 255u : 0u)) return 0;
    }
    return !strcmp(lab_last, text);
}
static int rot_plane_ok(const char *ref, uint32_t o, uint32_t iw, uint32_t ih)
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, ref); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != iw * ih * 3u) return 0;
    const uint32_t dw = o >= 4u ? ih : iw, dh = o >= 4u ? iw : ih, px = (800u - dw) / 2u, py = dh < 720u ? (720u - dh) / 2u : 0u, rows = dh > 720u ? dh : 720u;
    uint8_t *want = calloc((size_t)rows * 2400u, 1);
    for (uint32_t y = 0; y < ih; y++) for (uint32_t x = 0; x < iw; x++) { uint32_t X, Y; ref_turn(o, iw, ih, x, y, &X, &Y); for (uint32_t k = 0; k < 3u; k++) want[(py + Y) * 2400u + (px + X) * 3u + k] = d[(y * iw + x) * 3u + k]; }
    uint32_t bad = 0;
    for (uint32_t b = 0; b < rows * 2400u; b++) { if (lab_skip(GAL_ROT_BASE + b / 2u)) continue; const uint8_t got = (uint8_t)(mem[GAL_ROT_BASE + b / 2u] >> (8u * (b & 1u))); if (got != want[b]) bad++; }
    free(want); free(d); return bad == 0u;
}

/* the plane that is shown (upright, or turned by o) against the reference decode turned here, black outside the crop [x0, x1) x [y0, y1) of the plane's own coordinates (no crop: 0, 0, 800, rows) */
static int plane_mask_ok(const char *ref, uint32_t o, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, ref); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != 800u * 720u * 3u) return 0;
    const uint32_t iw = 800u, ih = 720u, dw = o >= 4u ? ih : iw, dh = o >= 4u ? iw : ih, px = (800u - dw) / 2u, py = dh < 720u ? (720u - dh) / 2u : 0u, rows = dh > 720u ? dh : 720u, base = o ? GAL_ROT_BASE : GAL_PLANE_BASE;
    uint8_t *want = calloc((size_t)rows * 2400u, 1);
    for (uint32_t y = 0; y < ih; y++) for (uint32_t x = 0; x < iw; x++) { uint32_t X, Y; ref_turn(o, iw, ih, x, y, &X, &Y); for (uint32_t k = 0; k < 3u; k++) want[(py + Y) * 2400u + (px + X) * 3u + k] = d[(y * iw + x) * 3u + k]; }
    for (uint32_t y = 0; y < rows; y++) for (uint32_t x = 0; x < 800u; x++) if (x < x0 || x >= x1 || y < y0 || y >= y1) for (uint32_t k = 0; k < 3u; k++) want[y * 2400u + x * 3u + k] = 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < rows * 2400u; b++) { if (lab_skip(base + b / 2u)) continue; const uint8_t got = (uint8_t)(mem[base + b / 2u] >> (8u * (b & 1u))); if (got != want[b]) bad++; }
    free(want); free(d); return bad == 0u;
}
static int plane_is(uint32_t pic)                                                                 /* the plane equals the canvas of picture `pic` byte for byte */
{
    char f[600]; snprintf(f, sizeof f, "%s/p%02u.tgy", dir, pic); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) { if (lab_skip(0x200000u + b / 2u)) continue; const uint8_t got = (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))); if (got != d[20480u + b]) bad++; }
    free(d); return bad == 0u;
}
static int plane_is_rgb(const char *fname)                                                       /* the plane equals the 800 x 720 RGB888 file `fname` (the reference decode of a version 1 picture) */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != 1728000u) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) if (!lab_skip(0x200000u + b / 2u) && (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))) != d[b]) bad++;
    free(d); return bad == 0u;
}
static int plane_is_f(const char *fname)                                                         /* the plane equals the canvas of the file `fname` of the set directory */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) { if (lab_skip(0x200000u + b / 2u)) continue; const uint8_t got = (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))); if (got != d[20480u + b]) bad++; }
    free(d); return bad == 0u;
}
static int plane_outline_is(const char *fname, int32_t rx, int32_t ry, int32_t rw, int32_t rh)      /* the plane is the canvas of `fname` with a 2-pixel ACC frame on the rectangle and nothing else changed */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    const uint16_t ac = th_role[TR_ACCENT]; const uint8_t col[3] = { (uint8_t)(((ac >> 11) & 31u) << 3), (uint8_t)(((ac >> 5) & 63u) << 2), (uint8_t)((ac & 31u) << 3) };
    uint32_t bad = 0, framed = 0;
    for (int32_t y = 0; y < 720; y++) for (int32_t x = 0; x < 800; x++) {
        const int in = x >= rx && x < rx + rw && y >= ry && y < ry + rh, fr = in && (x < rx + 2 || x >= rx + rw - 2 || y < ry + 2 || y >= ry + rh - 2);
        for (int c = 0; c < 3; c++) { const uint32_t b = ((uint32_t)y * 800u + (uint32_t)x) * 3u + (uint32_t)c; const uint8_t got = (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))); if (got != (fr ? col[c] : d[20480u + b])) bad++; }
        framed += fr;
    }
    free(d); return bad == 0u && framed > 100u;
}
static int v1_view_matches(const char *fname, uint32_t W, uint32_t H)                           /* what the scan-out shows (the window from the base and stride registers) equals the 720 x 800 piece of the 1:1 reference at the corner the firmware recorded */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != W * H * 3u) return 0;
    int ok = vstride == GV_WIN_STRIDE_WORDS && z_shown && z_sx >= 0 && z_sy >= 0 && (uint32_t)z_sx + 800u <= W && (uint32_t)z_sy + 720u <= H && (z_sx & 3) == 0;
    for (uint32_t r = 0; r < 720u && ok; r++) for (uint32_t i = 0; i < 1200u && ok; i++) {
        const uint32_t o = ((uint32_t)z_sy + r) * W * 3u + (uint32_t)z_sx * 3u + 2u * i; const uint16_t want = (uint16_t)(d[o] | d[o + 1u] << 8);
        if (mem[vbase + r * vstride + i] != want) ok = 0;
    }
    free(d); return ok;
}
static int z2_view_ok(const char *fname, uint32_t W, uint32_t H, uint32_t L)                                  /* the zoom view of effective level L (2 or 4): what the scan-out shows (the base and stride registers, and the doubling bit the firmware set: a plane 1:2 of the screen when the core doubles) is the piece of the level at the corner the firmware recorded, every source pixel an L x L block of the screen, pixel for pixel */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != W * H * 3u) return 0;
    const uint32_t stride = z2_pitch() / 2u, plane_hw = stride * z2_ty * z2_ot(), S = z2_s, DB = (uint32_t)z2_dbl; uint32_t p = 0u;
    if (z2_level == L) for (uint32_t q = 0; q < 2u; q++) if (vbase >= z2_base(q) && vbase < z2_base(q) + plane_hw) p = q;
    const uint32_t pb = z2_level == L ? z2_base(p) : 0u;
    int ok = z2_shown && z2_level == L && vstride == stride && vbase >= pb && (z2_vx & 3) == 0 && z2_vx >= 0 && z2_vy >= 0 && (uint32_t)z2_vx + (uint32_t)z2_vw <= S * W && (uint32_t)z2_vy + (uint32_t)z2_vh <= S * H && ((pix_ctrl & 8u) != 0u) == (DB == 2u);
    if (ok) { const uint32_t rel = vbase - pb, row = rel / stride, rem = rel % stride; if (rem % 3u || (int32_t)(z2_wtx[p] * z2_ot() + rem / 3u * 2u) != z2_vx || (int32_t)(z2_wty[p] * z2_ot() + row) != z2_vy) ok = 0; }
    for (uint32_t r = 0; r < 720u && ok; r++) for (uint32_t x = 0; x < 800u && ok; x++) for (uint32_t k = 0; k < 3u; k++) {
        const uint32_t px = x / DB, py = r / DB, b = 3u * px + k;
        if (lab_on && py >= lab_y0) continue;                                                                                    /* the name bar is on those rows (it is checked on its own) */
        const uint32_t got = (mem[vbase + py * vstride + b / 2u] >> (8u * (b & 1u))) & 255u;
        if (got != d[(((uint32_t)z2_vy + py) / S * W + ((uint32_t)z2_vx + px) / S) * 3u + k]) ok = 0;
    }
    free(d); return ok;
}
static uint32_t ph_stall, ph_stall_max, ph_jump_max, ph_axx, ph_axy; static int32_t ph_px = -1, ph_py = -1;
static void ldr_probe(uint32_t dst)      /* a chunk of a picture landing: what the loader bar shows before it (a track at the start, a fill that only grows). An open that shows no progress (a picture change in the viewer) leaves the last bar full under the viewer: its chunks are not judged */
{
    const uint32_t W = ldr_w, X = ldr_x, Y = ldr_y, H = ldr_h; uint32_t fill = 0u; for (uint32_t i = 0; i < W; i++) if (fb[Y * 512u + X + i] == th_role[TR_ACCENT]) fill++;
    if (dst == 0x40000000u) {
        ldr_skip = !ldr_live || fill == W; ldr_prev = 0u;
        if (!ldr_skip) { const int track = fb[Y * 512u + X + W - 1u] == (uint16_t)((th_role[TR_TEXT_SECONDARY] >> 1) & 0x7BEFu); ldr_opens++; if (!(track && fill == 0u && fb[(Y + H - 1u) * 512u + X + W - 1u] == fb[Y * 512u + X + W - 1u] && fb[(Y - 1u) * 512u + X] != th_role[TR_ACCENT] && fb[(Y + H) * 512u + X] != th_role[TR_ACCENT])) ldr_first_ok = 0u; }
    }
    if (ldr_skip) return;
    if (fill < ldr_prev) ldr_mono = 0u;
    if (fill > 0u && (fb[(Y + H) * 512u + X] == th_role[TR_ACCENT] || fb[(Y - 1u) * 512u + X] == th_role[TR_ACCENT] || fb[(Y + H - 1u) * 512u + X] != th_role[TR_ACCENT])) ldr_mono = 0u;      /* the bar is two rows: 178 and 179, no more */
    ldr_prev = fill; if (fill > ldr_max) ldr_max = fill; if (fill * 100u / W > ldr_pct) ldr_pct = fill * 100u / W; ldr_chunks++;
}
static void ldr_lead_probe(void)      /* a window has just been placed (a re-centre started, or the one being composed was aimed again): on an axis where the wanted corner is more than the follow slack (96) from the shown view, the window is placed for a pan one way or the other (the keys' way, else toward the wanted corner): GV_BEHIND to GV_BEHIND + 127 behind the view the firmware aimed at on one side, plus a little for the pixels the shown view has moved since. Not judged at an edge of the level */
{
    const uint32_t b = 1u - t_front; const int32_t sw = (int32_t)zl.ntx * 128, sh = (int32_t)zl.nty * 128;
    int32_t px = z_sx + (z_wx - z_sx < -96 ? -96 : z_wx - z_sx > 96 ? 96 : z_wx - z_sx), py = z_sy + (z_wy - z_sy < -96 ? -96 : z_wy - z_sy > 96 ? 96 : z_wy - z_sy);
    gv_clamp(&z_stop, &px, &py);
    if (z_wx - z_sx > 96 || z_sx - z_wx > 96) { const int32_t b1 = px - t_wx[b], b2 = (t_wx[b] + GV_WIN_W) - (px + 800); const int ok1 = b1 >= GV_BEHIND - 16 && b1 <= GV_BEHIND + 127 + 16, ok2 = b2 >= GV_BEHIND - 16 && b2 <= GV_BEHIND + 127 + 16; if (t_wx[b] > 0 && t_wx[b] + GV_WIN_W < sw) { rc_lead_n++; if (!ok1 && !ok2) rc_lead_bad++; } }
    if (z_wy - z_sy > 96 || z_sy - z_wy > 96) { const int32_t b1 = py - t_wy[b], b2 = (t_wy[b] + GV_WIN_H) - (py + 720); const int ok1 = b1 >= GV_BEHIND - 16 && b1 <= GV_BEHIND + 127 + 16, ok2 = b2 >= GV_BEHIND - 16 && b2 <= GV_BEHIND + 127 + 16; if (t_wy[b] > 0 && t_wy[b] + GV_WIN_H < sh) { rc_lead_n++; if (!ok1 && !ok2) rc_lead_bad++; } }
}
static uint32_t zbp_n, zbp_max, zbp_prev, zbp_bad, zbp_rmax;
static void zb_probe(void)      /* the zoom's loader bar in the fit plane (rows 358 and 359, pixels 320 to 479): a prefix of ACC pixels on a dark track, a prefix that only grows, the same in both rows */
{
    if (!zoom_phase || zk < 0 || z_shown || !zb_on) return;
    { static uint32_t last_t0; if (z_t0 != last_t0) { last_t0 = z_t0; zbp_prev = 0u; } }                      /* a zoom opened again (B goes back a level at a time now, so a level can open on the way to the fit view): its bar starts from 0 */
    const uint16_t ac = th_role[TR_ACCENT], tr = (uint16_t)((th_role[TR_TEXT_SECONDARY] >> 1) & 0x7BEFu);
    const uint8_t ar = (uint8_t)(((ac >> 11) & 31u) << 3), ag = (uint8_t)(((ac >> 5) & 63u) << 2), ab = (uint8_t)((ac & 31u) << 3), tR = (uint8_t)(((tr >> 11) & 31u) << 3), tG = (uint8_t)(((tr >> 5) & 63u) << 2), tB = (uint8_t)((tr & 31u) << 3);
    uint32_t fill[8];
    for (uint32_t r = 0; r < zb_rows; r++) {
        uint32_t f = 0u, inprefix = 1u;
        for (uint32_t px = 0; px < zb_w; px++) {
            const uint32_t byte0 = px * 3u; uint8_t v[3];
            for (uint32_t c = 0; c < 3u; c++) { const uint32_t bi = byte0 + c; v[c] = (uint8_t)(mem[zb_view + r * zb_stride + bi / 2u] >> (8u * (bi & 1u))); }
            const int isf = v[0] == ar && v[1] == ag && v[2] == ab, ist = v[0] == tR && v[1] == tG && v[2] == tB;
            if (isf && inprefix) f++; else { inprefix = 0u; if (!ist) zbp_bad++; }
        }
        fill[r] = f;
    }
    for (uint32_t r = 1; r < zb_rows; r++) if (fill[r] != fill[0]) zbp_bad++;
    if (fill[0] < zbp_prev) zbp_bad++;
    zbp_prev = fill[0]; if (fill[0] > zbp_max) zbp_max = fill[0]; if (zl.layout == 0u && fill[0] > zbp_rmax) zbp_rmax = fill[0]; zbp_n++;
}
static uint32_t to_n, to_bad;
static void tile_read_probe(uint32_t off)      /* a tile of a tile level is about to be read: it is the first tile of the list for the pan's direction (the keys' way, else toward the wanted corner) that is not whole yet, in the window being filled. The direction is worked out here from the keys the script gave, not from the firmware's variables */
{
    if (zl.layout != 1u || zk < 0 || !zb_ok_phase) return;
    const int dxk = (last_key & KEY_RIGHT) ? 1 : (last_key & KEY_LEFT) ? -1 : 0, dyk = (last_key & KEY_DOWN) ? 1 : (last_key & KEY_UP) ? -1 : 0;
    int32_t px = z_wx, py = z_wy;
    if (z_shown) { px = z_sx + (z_wx - z_sx < -96 ? -96 : z_wx - z_sx > 96 ? 96 : z_wx - z_sx); py = z_sy + (z_wy - z_sy < -96 ? -96 : z_wy - z_sy > 96 ? 96 : z_wy - z_sy); gv_clamp(&z_stop, &px, &py); }
    const int lxk = dxk ? dxk : (z_wx > px ? 1 : z_wx < px ? -1 : 0), lyk = dyk ? dyk : (z_wy > py ? 1 : z_wy < py ? -1 : 0);
    const uint32_t tw = t_recentre ? 1u - t_front : t_front, idx = (off - zl.off) / 49152u; const int32_t tx = (int32_t)(idx % zl.ntx), ty = (int32_t)(idx / zl.ntx);
    const int32_t cx = (t_recentre && z_shown) ? z_sx : px, cy = (t_recentre && z_shown) ? z_sy : py;
    gv_tile_t need[GV_MAX_TILES]; const uint32_t n = gv_tiles_needed(&z_stop, cx, cy, t_wx[tw], t_wy[tw], lxk, lyk, need);
    for (uint32_t i = 0; i < n; i++) {
        int col, row; if (!gv_win_tile(t_wx[tw], t_wy[tw], need[i].tx, need[i].ty, &col, &row) || t_res[tw][row * GV_WIN_TX + col]) continue;
        to_n++; if (need[i].tx != tx || need[i].ty != ty) to_bad++; return;
    }
    to_bad++;
}
static void pass_hook(void)                                                                  /* a tile zoom: how far the shown view moved since the last pass, and for how many passes in a row it stood still while the wanted corner was elsewhere */
{
    zb_probe();
    if (!zoom_phase || zk < 0 || zl.layout != 1u || !z_shown) { ph_px = -1; ph_stall = 0u; return; }
    if (ph_px >= 0) { const int32_t jx = abs(z_sx - ph_px), jy = abs(z_sy - ph_py), j = jx > jy ? jx : jy; if ((uint32_t)j > ph_jump_max) ph_jump_max = (uint32_t)j; }
    if (t_recentre && !rc_prev) { rc_starts++; ldr_lead_probe(); } else if (t_recentre && rc_prev && (t_wx[1u - t_front] != rc_bx || t_wy[1u - t_front] != rc_by)) { rc_retarget++; ldr_lead_probe(); }      /* a re-centre that started, and a window being composed that was aimed again */
    rc_prev = t_recentre; rc_bx = t_wx[1u - t_front]; rc_by = t_wy[1u - t_front];
    if (ph_px >= 0 && z_sx != ph_px && z_sy == ph_py && z_wy != z_sy) ph_axx++;      /* a step along x alone while the wanted corner is off in y too: the one-axis fallback */
    if (ph_px >= 0 && z_sy != ph_py && z_sx == ph_px && z_wx != z_sx) ph_axy++;
    const int behind = z_sx != z_wx || z_sy != z_wy;
    if (ph_px >= 0 && z_sx == ph_px && z_sy == ph_py && behind) { if (++ph_stall > ph_stall_max) ph_stall_max = ph_stall; } else ph_stall = 0u;
    ph_px = z_sx; ph_py = z_sy;
}
static int count_text(const char *s) { int n = 0; for (int i = 0; i < 64; i++) if (!strcmp(texts[i], s)) n++; return n; }
static int said(const char *s) { for (int i = 0; i < 64; i++) if (strstr(texts[i], s)) return 1; return 0; }
static int text_y(const char *s) { for (int k = 1; k <= 64; k++) { const int i = (ntext + 64 - k) % 64; if (strstr(texts[i], s)) return (int)texts_y[i]; } return -1; }      /* the y of the newest text that holds s, -1 if none */
static const char *last_text(void) { return texts[(ntext + 63) % 64]; }
static void clear_texts(void) { memset(texts, 0, sizeof texts); ntext = 0; }

/* ---- the version 1 library scenario (folders, grid sizes, rounded corners, selection ring): the checks use their own copy of the geometry (spec numbers), not the firmware's table ---- */
static int lib1_on; static uint32_t gsel;                                                                                   /* the grid under test */
static void dump_fb(const char *name)                                                                                 /* FB_DUMP=dir: the 400 x 360 screen as a binary PPM, to look at a page (not part of any check) */
{
    const char *dir = getenv("FB_DUMP"); if (!dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name); FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n400 360\n255\n");
    for (uint32_t y = 0; y < 360u; y++) for (uint32_t x = 0; x < 400u; x++) { const uint16_t v = fb[y * 512u + x]; const uint8_t c[3] = { (uint8_t)(((v >> 11) & 31u) * 255u / 31u), (uint8_t)(((v >> 5) & 63u) * 255u / 63u), (uint8_t)((v & 31u) * 255u / 31u) }; fwrite(c, 1, 3, f); }
    fclose(f);
}
static int preview_ok(const uint8_t *t, uint32_t y0)                                                  /* the details page's small image: the 96 x 96 thumbnail at x 6, every pixel but the rounded corners' 8 x 8 squares */
{
    for (uint32_t r = 0; r < 96u; r++) for (uint32_t c = 0; c < 96u; c++) {
        if ((r < 8u || r >= 88u) && (c < 8u || c >= 88u)) continue;
        const uint32_t o = (r * 96u + c) * 2u; if (fb[(y0 + r) * 512u + 6u + c] != (uint16_t)(t[o] | t[o + 1] << 8)) return 0;
    }
    return 1;
}
static const uint8_t *thumb1_of(uint32_t i) { const uint32_t atlas = (uint32_t)idx_data[24] | (uint32_t)idx_data[25] << 8 | (uint32_t)idx_data[26] << 16; return idx_data + atlas + i * 18432u; }
typedef struct { uint32_t cols, rows, cw, x0, dx, y0, dy, rad, ratio; } lg_t;                                  /* ratio: 0 = 1:1, 1 = 3:4, 2 = 2:3, 3 = 1:2 */
static const lg_t LG[4] = { { 4, 3, 96, 3, 99, 6, 102, 8, 0 }, { 5, 4, 72, 6, 78, 4, 76, 8, 1 }, { 6, 4, 64, 2, 66, 10, 74, 6, 2 }, { 8, 6, 48, 1, 49, 3, 51, 4, 3 } };
static int cut_pixel(uint32_t r, uint32_t c, uint32_t w, uint32_t h, uint32_t rad)                             /* is pixel (r, c) of a w x h cell in a corner cut of radius rad (the circle model of the stub) */
{
    for (uint32_t cy = 0; cy < 2u; cy++) for (uint32_t cx = 0; cx < 2u; cx++) {
        const uint32_t dy = cy ? h - 1u - r : r, k = cx ? w - 1u - c : c;
        if (dy < rad) { const double dd = (double)rad - (double)dy - 0.5; const uint32_t in = (uint32_t)floor(sqrt((double)rad * rad - dd * dd) + 0.5), cut = rad > in ? rad - in : 0u; if (k < cut) return 1; }
    }
    return 0;
}
static uint16_t box_avg(const uint8_t *t, uint32_t sx, uint32_t sy)                                              /* the down-scale op: the rounded average of the 2 x 2 source box at (sx, sy) of a 96 x 96 thumbnail */
{
    uint32_t r5 = 0, g6 = 0, b5 = 0;
    for (uint32_t q = 0; q < 4u; q++) { const uint32_t o = ((sy + (q >> 1)) * 96u + sx + (q & 1u)) * 2u; const uint16_t p = (uint16_t)(t[o] | t[o + 1] << 8); r5 += p >> 11; g6 += (p >> 5) & 63u; b5 += p & 31u; }
    return (uint16_t)((((r5 + 2u) >> 2) << 11) | (((g6 + 2u) >> 2) << 5) | ((b5 + 2u) >> 2));
}
/* cell k of grid g shows thumbnail `t` (strip: a folder tile, its bottom 16 rows are the name strip and are not compared); the corners are the background colour */
static int lib1_cell_ok(uint32_t g, uint32_t k, const uint8_t *t, int strip)
{
    const lg_t *L = &LG[g]; const uint32_t x = L->x0 + (k % L->cols) * L->dx, y = 22u + L->y0 + (k / L->cols) * L->dy, rows = strip ? L->cw - 16u : L->cw;
    static const uint32_t NUM[4] = { 1, 3, 2, 1 }, DEN[4] = { 1, 4, 3, 2 };
    for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < L->cw; c++) {
        const uint16_t got = fb[(y + r) * 512u + x + c];
        if (cut_pixel(r, c, L->cw, L->cw, L->rad)) { if (got != th_role[TR_BG_BOTTOM] && got != th_role[TR_ACCENT]) return 0; continue; }
        uint16_t want;
        if (L->ratio == 0u) { const uint32_t o = (r * 96u + c) * 2u; want = (uint16_t)(t[o] | t[o + 1] << 8); }
        else want = box_avg(t, c * DEN[L->ratio] / NUM[L->ratio], r * DEN[L->ratio] / NUM[L->ratio]);
        if (got != want) return 0;
    }
    return 1;
}
static int lib1_page_ok(uint32_t g, uint32_t first, uint32_t n) { for (uint32_t k = 0; k < n; k++) if (!lib1_cell_ok(g, k, thumb1_of(first + k), 0)) return 0; return 1; }
/* the ring of the selected cell: the pixel left of its middle is the accent colour, the cell's own corner notch too (when rounded) and the ring's outer corner is the background; every other cell has background there */
static int lib1_ring_ok(uint32_t g, uint32_t cells, uint32_t sel_k)
{
    const lg_t *L = &LG[g]; const uint32_t t = g == 3u ? 1u : 2u, w = L->cw;
    const uint32_t sx = L->x0 + (sel_k % L->cols) * L->dx, sy = 22u + L->y0 + (sel_k / L->cols) * L->dy;         /* the selected cell */
    /* the expected colour of a pixel NEXT TO a cell: the accent colour when it is in the selected cell's frame (the box grown by t, rounded outside by rad + t), else the background */
    #define RING_WANT(X, Y) (((int)(X) >= (int)sx - (int)t && (int)(X) < (int)(sx + w + t) && (int)(Y) >= (int)sy - (int)t && (int)(Y) < (int)(sy + w + t) && !((int)(X) >= (int)sx && (int)(X) < (int)(sx + w) && (int)(Y) >= (int)sy && (int)(Y) < (int)(sy + w)) \
        && !(L->rad && cut_pixel((Y) - sy + t, (X) - sx + t, w + 2u * t, w + 2u * t, L->rad + t))) ? th_role[TR_ACCENT] : th_role[TR_BG_BOTTOM])
    for (uint32_t k = 0; k < cells; k++) {
        const uint32_t x = L->x0 + (k % L->cols) * L->dx, y = 22u + L->y0 + (k / L->cols) * L->dy;
        if (fb[(y + w / 2u) * 512u + x - 1u] != RING_WANT(x - 1u, y + w / 2u) || fb[(y - 1u) * 512u + x + w / 2u] != RING_WANT(x + w / 2u, y - 1u)
            || fb[(y + w / 2u) * 512u + x + w] != RING_WANT(x + w, y + w / 2u) || fb[(y + w) * 512u + x + w / 2u] != RING_WANT(x + w / 2u, y + w)) return 0;
        { uint32_t a = 0; for (; a < L->rad; a++) { const double d = ((double)L->rad - a - 0.5) * 1.41421356; if (d > L->rad + 0.3 && d < L->rad + t - 0.3) break; }       /* a pixel on the diagonal of the corner inside the thumbnail's notch yet inside the ring's outer circle: accent when selected, else background */
          if (a < L->rad && fb[(y + a) * 512u + x + a] != (k == sel_k ? th_role[TR_ACCENT] : th_role[TR_BG_BOTTOM])) return 0; }
        if (k == sel_k && fb[(y - t) * 512u + x - t] != th_role[TR_BG_BOTTOM]) return 0;                          /* the frame is rounded outside */
    }
    #undef RING_WANT
    return 1;
}
static int hist_dec_ok(void)                                                                                          /* the details page's histograms against a count of the decoded pixels on the plane (the picture's own rectangle), written here from the definition: luma (77 R + 150 G + 29 B) >> 8, 8 bins of 256 to one of 32, the 16 bit rule of the display */
{
    static uint32_t c[4][256]; memset(c, 0, sizeof c); uint32_t n = 0;
    for (uint32_t y = pic.ry; y < (uint32_t)pic.ry + pic.rh; y++) for (uint32_t x = pic.rx; x < (uint32_t)pic.rx + pic.rw; x++) {
        const uint32_t a = GAL_PLANE_BASE * 2u + y * 2400u + x * 3u, r = mbyte(a), g = mbyte(a + 1u), b = mbyte(a + 2u);
        c[0][(77u * r + 150u * g + 29u * b) >> 8]++; c[1][r]++; c[2][g]++; c[3][b]++; n++;
    }
    for (int k = 0; k < 4; k++) for (int i = 0; i < 256; i++) c[k][i] *= hist_boost;
    uint32_t mx = 0u, s = 0u; for (int k = 0; k < 4; k++) for (int i = 0; i < 256; i++) if (c[k][i] > mx) mx = c[k][i];
    while ((mx >> s) > 65535u) s++;
    int ok = det_hist.n == n && n > 0u;
    for (int k = 0; k < 4; k++) for (int i = 0; i < 256; i++) { uint32_t v = c[k][i] >> s; if (!v && c[k][i]) v = 1u; if (det_hist.fine[k][i] != v) ok = 0; }
    ok = ok && det_hist.is_fine == 1u;
    return ok;
}
static void lib1_cp(int n)
{
    char w[32];
    if (getenv("LIB1_DEBUG")) { printf("cp %d: level %u gi %u first %u count %u sel %u page %u nop %u last '%s' c0 %d c1 %d c2 %d ring0 %d ring1 %d pageok %d\n", n, level, gi, cur_first, cur_count, sel, page, n_on_page, last_text(), lib1_cell_ok(0, 0, thumb1_of(0), 1), lib1_cell_ok(0, 1, thumb1_of(20), 1), lib1_cell_ok(0, 2, thumb1_of(23), 1), lib1_ring_ok(0, 3, 0), lib1_ring_ok(0, 3, 1), lib1_page_ok(0, cur_first + page * pp, n_on_page)); }
    switch (n) {
    case 400: chk(400, level == 1u && gi == 0u && cur_count == 3u && sel == 0u && !strcmp(last_text(), "1/3") && lib1_cell_ok(0, 0, thumb1_of(0), 1) && lib1_cell_ok(0, 1, thumb1_of(20), 1) && lib1_cell_ok(0, 2, thumb1_of(23), 1) && lib1_ring_ok(0, 3, 0)
              && said("Alpha 20") && said("Beta 3") && said("Gamma 13")); break;                                             /* the folder level: the covers, the names and counts in the strips, the ring on the first */
    case 401: chk(401, sel == 1u && !strcmp(last_text(), "2/3") && lib1_ring_ok(0, 3, 1)); break;                          /* Right: the ring moves to the second folder (no page redraw: the covers stay) */
    case 402: chk(402, level == 0u && cur_first == 20u && cur_count == 3u && sel == 20u && !strcmp(last_text(), "1/3") && lib1_page_ok(0, 20, 3) && lib1_ring_ok(0, 3, 0) && said("Img 20")); break;   /* A: the pictures of the second folder, three of them, in the 4 x 3 grid */
    case 403: chk(403, level == 1u && sel == 1u && !strcmp(last_text(), "2/3") && lib1_cell_ok(0, 2, thumb1_of(23), 1) && lib1_ring_ok(0, 3, 1)); break;      /* B: back on the folder level with the folder we left selected */
    case 404: chk(404, level == 0u && cur_first == 0u && cur_count == 20u && n_on_page == 12u && !strcmp(last_text(), "1/20") && lib1_page_ok(0, 0, 12) && lib1_ring_ok(0, 12, 0)); break;   /* Left, A: the first folder, 12 on a page */
    case 405: chk(405, page == 1u && n_on_page == 8u && sel == 12u && !strcmp(last_text(), "13/20") && lib1_page_ok(0, 12, 8) && lib1_ring_ok(0, 8, 0)); break;       /* R1: the next page of the folder */
    case 406: chk(406, sel == 19u && !strcmp(last_text(), "20/20") && lib1_page_ok(0, 12, 8) && lib1_ring_ok(0, 8, 7)); break;          /* R1 again: the last picture of the folder (not of the library) */
    case 407: chk(407, sel == 0u && page == 0u && !strcmp(last_text(), "1/20") && lib1_page_ok(0, 0, 12)); break;                   /* Right on a new press at the last picture wraps to the first of the folder */
    case 408: chk(408, level == 1u && sel == 0u && !strcmp(last_text(), "1/3")); break;                                              /* B */
    case 409: chk(409, sel == 2u && !strcmp(last_text(), "3/3")); break;                                                             /* Left on a new press at the first folder wraps to the last */
    case 410: chk(410, level == 0u && cur_first == 23u && cur_count == 13u && n_on_page == 12u && sel == 23u && !strcmp(last_text(), "1/13") && lib1_page_ok(0, 23, 12)); break;      /* A: the third folder, 13 pictures: 12 and 1 */
    case 411: chk(411, sel == 20u && strstr(last_open, "l20.tgy") && vid_bus == 1u); break;                                          /* in the viewer on the first picture of the second folder */
    case 412: chk(412, sel == 22u && strstr(last_open, "l22.tgy")); break;                                                           /* Left in the viewer wraps INSIDE the folder: the last picture of the folder, not the one before the first of the library */
    case 413: chk(413, sel == 20u && strstr(last_open, "l20.tgy")); break;                                                           /* Right wraps back */
    case 414: chk(414, vid_bus == 0u && level == 0u && sel == 21u && !strcmp(last_text(), "2/3") && lib1_page_ok(0, 20, 3) && lib1_ring_ok(0, 3, 1)); break;      /* B: the folder's grid with the picture shown last selected */
    case 430: case 440: { const uint32_t per = LG[gsel].cols * LG[gsel].rows, tot = n == 430 ? 20u : 60u, nn = per < tot ? per : tot; char f[16]; snprintf(f, sizeof f, "1/%u", tot);
              chk(n, level == 0u && gi == gsel && pp == per && cur_count == tot && n_on_page == nn && !strcmp(last_text(), f) && lib1_page_ok(gsel, cur_first, nn) && lib1_ring_ok(gsel, nn, 0) && corner_ops > 0u && dsblit_ops == (gsel ? nn : 0u) && dsblit_bad == 0u); break; }   /* the grid of the setting: every cell scaled by the engine (or as it is), rounded, the ring on the first */
    case 431: { const uint32_t cols = LG[gsel].cols, per = cols * LG[gsel].rows, nn = per < 20u ? per : 20u; char f[16]; snprintf(f, sizeof f, "%u/20", cols + 1u); chk(431, sel == cols && !strcmp(last_text(), f) && lib1_ring_ok(gsel, nn, cols)); break; }   /* Down: one row (cols entries) on */
    case 432: { const uint32_t cols = LG[gsel].cols, per = cols * LG[gsel].rows, nn = per < 20u ? per : 20u; chk(432, sel == cols + 1u && lib1_ring_ok(gsel, nn, cols + 1u)); break; }
    case 441: { const uint32_t per = LG[gsel].cols * LG[gsel].rows, n2 = 60u - per < per ? 60u - per : per; chk(441, page == 1u && sel == per && n_on_page == n2 && lib1_page_ok(gsel, per, n2) && lib1_ring_ok(gsel, n2, 0)); break; }   /* R1: the next page of the single folder */
    case 442: { const uint32_t per = LG[gsel].cols * LG[gsel].rows, want = 2u * per < 60u ? 2u * per : 59u; chk(442, sel == want && page == want / per); break; }
    case 450: { uint32_t sx, sy; sx = LG[0].x0; sy = 22u + LG[0].y0; const uint16_t dk = (uint16_t)((th_role[TR_BG_BOTTOM] >> 1) & 0x7BEFu);
              chk(450, level == 1u && th_role[TR_BG_BOTTOM] != 0u && dk != th_role[TR_BG_BOTTOM] && lib1_cell_ok(0, 0, thumb1_of(0), 1) && fb[(sy + 90u) * 512u + sx + 90u] == dk && fb[(sy + 79u) * 512u + sx + 90u] != dk); break; }   /* a folder tile in the light theme: the name strip is darker than the background and the cover above it is not */
    case 519: chk(519, pix_ctrl == 1u && pix_stores == 0u && pix_bad == 0u); break;                                                                                  /* at start: dither only (default ordered), no tables, nothing loaded */
    case 526: { const int yr = text_y("RESUME LAST PICTURE"), yg = text_y("GRID SIZE"), yd = text_y("DITHER"), yc = text_y("COLOUR"), yh = text_y("ON: the grid opens");      /* Settings: four rows, each its own line, and the hint under the last one (it was drawn over the COLOUR row) */
        chk(526, yr >= 0 && yg > yr && yd > yg && yc > yd && yh >= yc + 20); } break;
    case 520: chk(520, pix_ctrl == 5u && pix_stores == 768u && pix_bad == 0u && pix_tables_are(1u)); break;                                                             /* preset 1: tables on, dither bits kept, all 768 entries stored (none while busy or on) */
    case 521: chk(521, pix_ctrl == 5u && pix_stores == 1536u && pix_bad == 0u && pix_tables_are(2u)); break;                                                            /* the next preset: tables off while loading, on again, new curves */
    case 522: chk(522, pix_ctrl == 1u && pix_stores == 6912u && pix_tables_are(1u) && pix_bad == 0u); break;                                                                                  /* Left to preset 1 reloads it, Left again to NORMAL: tables off, nothing reloaded (the tables still hold preset 1) */
    case 524: chk(524, pix_ctrl == 5u && pix_stores == 3840u && pix_bad == 0u && pix_tables_are(5u)); break;                                                             /* WARM: the tint presets differ per channel, so a wrong channel shows */
    case 525: chk(525, pix_ctrl == 5u && pix_stores == 768u && pix_bad == 0u); break;                                                                                   /* a key that changes nothing (the row moves) does not reload the tables */
    case 523: chk(523, pix_ctrl == 5u && pix_stores == 7680u && pix_bad == 0u && pix_tables_are(7u)); break;                                                            /* Left from NORMAL wraps to INVERT */
    case 451: { const uint32_t per = LG[1].cols * LG[1].rows; chk(451, level == 0u && gi == 1u && pp == per && sel == 48u && page == 2u && n_on_page == 20u && lib1_page_ok(1, 40, 20) && lib1_ring_ok(1, 20, 8) && !strcmp(last_text(), "49/60")); break; }   /* the grid size changed in Settings (4 x 3 to 5 x 4): the same picture stays selected, on its page of the new grid */
    case 490: chk(490, level == 0u && sel == 0u && lib1_page_ok(0, 0, 12)); clear_texts(); break;
    case 491: dump_fb("info"); chk(491, said("DETAILS  INFO") && said("Canon EOS R5") && said("RF24-70mm F2.8") && said("2024-05-17 13:04") && said("JPEG") && said("1/4") && said("Img 00") && fb[9u * 512u + 324u] == th_role[TR_ACCENT] && fb[9u * 512u + 332u] == th_role[TR_TEXT_SECONDARY] && fb[9u * 512u + 348u] == th_role[TR_TEXT_SECONDARY] && said("IN THE PICTURE") && said("X  NEXT COLOUR FILTER") && said("Y  TURN RIGHT") && said("L1  CROP MASK") && preview_ok(thumb1_of(sel), 168u)); clear_texts(); break;      /* Select on a picture with every detail (the three dots: the first, current page, in the accent colour): INFO has FORMAT, CAMERA, LENS, TAKEN; three pages */
    case 492: chk(492, said("DETAILS  SHOT") && said("1/250 S") && said("F/2.8") && said("400") && said("35 MM") && said("2/4") && !said("Canon")); clear_texts(); break;      /* Right: the SHOT page */
    case 493: dump_fb("hist_thumb"); chk(493, said("DETAILS  HISTOGRAMS") && said("3/4") && det_hist.n == 96u * 96u && det_hist.is_fine == 0u && said("LEVELS") && said("32") && said("PIXELS") && said("9216") && preview_ok(thumb1_of(sel), 26u)); clear_texts(); break;                                      /* Right: the histograms count every pixel of the square thumbnail */
    case 494: chk(494, said("DETAILS  INFO") && said("1/4")); clear_texts(); break;                                                                       /* Right twice (the overlay page, then) wraps to the first page */
    case 495: chk(495, said("DETAILS  HISTOGRAMS") && said("3/4")); clear_texts(); break;                                                                  /* Left from the first wraps to the last */
    case 496: chk(496, level == 0u && sel == 0u && lib1_page_ok(0, 0, 12) && !said("DETAILS")); clear_texts(); break;                                         /* B: the grid again */
    case 497: chk(497, said("DETAILS  INFO") && said("PNG") && !said("CAMERA") && !said("LENS") && !said("TAKEN") && said("1/3") && said("Img 01")); clear_texts(); break;      /* a picture that has only its format: FORMAT and nothing else, two pages (no SHOT) */
    case 498: chk(498, said("DETAILS  HISTOGRAMS") && said("2/3")); clear_texts(); break;
    case 499: chk(499, said("DETAILS  INFO") && !said("FORMAT") && !said("CAMERA") && !said("TAKEN") && said("1/3") && said("Img 02")); clear_texts(); break;      /* a picture with no details: the four rows only */
    case 421: chk(421, level == 0u && cur_first == 0u && cur_count == 20u && sel == 5u && !strcmp(last_text(), "6/20") && lib1_page_ok(0, 0, 12)); break;                  /* resume on a picture of the FIRST folder */
    case 422: chk(422, level == 0u && cur_first == 23u && cur_count == 13u && sel == 30u && page == 0u && !strcmp(last_text(), "8/13") && lib1_page_ok(0, 23, 12)); break;      /* resume on a picture of the LAST folder */
    case 420: chk(420, level == 0u && cur_first == 20u && cur_count == 3u && sel == 21u && !strcmp(last_text(), "2/3") && lib1_page_ok(0, 20, 3)); break;      /* the resume scenario: boots inside the folder of the saved picture, on it */
    default: (void)w; break;
    }
}

static int view_matches(const char *fname, int32_t *ox, int32_t *oy)                              /* what the scan-out would show (from the base and stride registers) equals the raster zoom level of the file, pixel for pixel */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    gt_zoom_t z; int found = 0; for (uint32_t k = 0; k < 8u && !found; k++) { if (gt_zoom_level(d, n < 4096u ? n : 4096u, k, &z)) break; if (z.layout == 0u) found = 1; }
    int ok = found && vstride == z.stride / 2u && vbase >= GAL_ZOOM_BASE;
    if (ok) {
        const uint32_t rel = vbase - GAL_ZOOM_BASE, y = rel / vstride, rem = rel % vstride; const int32_t x = (int32_t)(rem / 6u * 4u);
        if (rem % 6u || y + 720u > z.h || (uint32_t)x + 800u > z.w || x != z_sx || (int32_t)y != z_sy) ok = 0;                 /* the registers say what the firmware recorded it showed */
        else { *ox = x; *oy = (int32_t)y;
            for (uint32_t r = 0; r < 720u && ok; r++) for (uint32_t i = 0; i < 1200u; i++) {
                const uint32_t o = z.off + (y + r) * z.stride + (uint32_t)x * 3u + 2u * i; const uint16_t want = (uint16_t)(d[o] | d[o + 1u] << 8);
                if (mem[vbase + r * vstride + i] != want) { ok = 0; break; } } }
    }
    free(d); return ok;
}
static int tile_view_matches(const char *fname, int32_t *ox, int32_t *oy)                       /* the same for a tile level shown from a resident window: the base and stride decode to a window, a corner and 720 lines of 1,200 halfwords */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    gt_zoom_t z; int found = 0; for (uint32_t k = 0; k < 8u && !found; k++) { if (gt_zoom_level(d, n < 4096u ? n : 4096u, k, &z)) break; if (z.layout == 1u) found = 1; }
    int ok = found && vstride == 2112u && vbase >= T_WIN0 && vbase < T_STAGE;
    if (ok) {
        const uint32_t w = vbase >= T_WIN1 ? 1u : 0u, rel = vbase - (w ? T_WIN1 : T_WIN0), row = rel / 2112u, cx = rel % 2112u;
        const int32_t x = t_wx[w] + (int32_t)(cx / 6u * 4u), y = t_wy[w] + (int32_t)row;
        if (cx % 6u || x != z_sx || y != z_sy || w != t_shown_win || x + 800 > (int32_t)(z.ntx * 128u) || y + 720 > (int32_t)(z.nty * 128u)) ok = 0;
        else { *ox = x; *oy = y;
            for (uint32_t r = 0; r < 720u && ok; r++) for (uint32_t i = 0; i < 1200u; i++) {
                const uint32_t bc = (uint32_t)x * 3u + 2u * i, tx = bc / 384u, within = bc % 384u, ra = (uint32_t)y + r, ty = ra / 128u;
                const uint32_t o = z.off + ((ty * z.ntx + tx) * 128u + ra % 128u) * 384u + within;
                if (mem[vbase + r * 2112u + i] != (uint16_t)(d[o] | d[o + 1u] << 8)) { ok = 0; break; } } }
    }
    free(d); return ok;
}
static int fit_or_exact_tile(const char *fname) { int32_t x, y; return vbase == GAL_PLANE_BASE || tile_view_matches(fname, &x, &y); }
static int fit_or_exact(const char *fname) { int32_t x, y; return vbase == GAL_PLANE_BASE || view_matches(fname, &x, &y); }      /* while loading, the fit plane may still be shown; once the zoom plane is shown it must be exact */
static int big_grid_ok(void)                                                                      /* a large index: the page on screen is the page of the selection, with its thumbnails, its counter and its frame */
{
    const uint32_t pg = sel / 16u, first = pg * 16u, n = count - first < 16u ? count - first : 16u; char w[32]; snprintf(w, sizeof w, "%u/%u", sel + 1u, count);
    const uint32_t k = sel - first, fx = 2u + (k % 4u) * 100u - 2u, fy = 22u + (k / 4u) * 76u - 2u;
    for (uint32_t e = n; e < 16u; e++) { const uint32_t x = 2u + (e % 4u) * 100u, y = 22u + (e / 4u) * 76u; for (uint32_t r = 0; r < 72u; r++) for (uint32_t c = 0; c < 96u; c++) if (fb[(y + r) * 512u + x + c] != th_role[TR_BG_BOTTOM]) return 0; }      /* the cells past the last picture stay empty */
    return page == pg && !strcmp(last_text(), w) && thumbs_on_screen(first, n) && fb[fy * 512u + fx] == th_role[TR_ACCENT];
}
static void clear_texts(void);
static void lib1_cp(int n);
static uint32_t starts_570, nl593; static int32_t z2_c0x, z2_c0y, z2_c1x, z2_c1y;
static int hid_pairs(void)                                                                                           /* a turn or a mask redraws a plane that is on the screen: each time the screen is pointed at the black plane first and at a picture plane after, never at a half drawn one */
{
    if (vsnap_n < 2u || (vsnap_n & 1u)) return 0;
    for (uint32_t i = 0; i < vsnap_n; i++) if ((vsnap[i] == 0x300000u) != ((i & 1u) == 0u)) return 0;
    return 1;
}
static void checkpoint(int n)
{
    char want[64]; char path[200];
    vsnap_n = vlog_n; memcpy(vsnap, vlog, vlog_n * sizeof vlog[0]); vlog_n = 0u;
    if (lib1_on) { lib1_cp(n); return; }
    if (getenv("APP_DEBUG") && n == 35) printf("cp35: thumbs %d px %04x bg %04x level %u gi %u sel %u page %u count %u last '%s'\n", thumbs_on_screen(0, count < 16u ? count : 16u), fb[100u * 512u + 399u], th_role[TR_BG_BOTTOM], level, gi, sel, page, count, last_text());
    switch (n) {
    case 200: chk(200, hs_starts == 0u && plane_is_rgb("v00.rgb") && tg.starts == 42u && tg.loads == 1u && !said("ERROR") && !said("DAMAGED")); clear_texts(); break;                          /* a version 1 picture opened from the grid: the plane is its decode, 42 tiles, one table load */
    case 201: chk(201, plane_is_rgb("v01.rgb") && tg.starts == 84u && tg.loads == 2u); break;                                                                                 /* the next picture (a 500 x 300 one-level file): another table load for another file at the same base */
    case 202: chk(202, plane_is_f("v02.tgy") && tg.starts == 84u); break;                                                                                                     /* a version 0 picture in the same library still lands as before (no decoder start) */
    case 203: chk(203, said("DECODER ERROR") && vid_bus == 0u); clear_texts(); break;                                                                                         /* a damaged stream: the viewer is left and the status line says so */
    case 204: chk(204, said("PICTURE DAMAGED")); clear_texts(); break;                                                                                                        /* a damaged tile index */
    case 205: chk(205, said("NOT A TGY PICTURE")); clear_texts(); break;                                                                                                      /* a damaged header */
    case 207: force_ovf = 1; v1_starts_before = tg.starts; clear_texts(); break;                                                                                              /* the landing path counts an overflow: the next open must not show a damaged picture */
    case 208: chk(208, said("PICTURE DAMAGED IN TRANSFER") && vid_bus == 0u && tg.starts == v1_starts_before); force_ovf = 0; clear_texts(); break;                           /* a version 1 picture whose landing overflowed is refused before the decoder starts */
    case 210: chk(210, plane_is_rgb("v00.rgb") && zn == 1u && zs[0].layout == 2u && zs[0].w == 1000u && zs[0].h == 900u && zk < 0); break;                                         /* the v1 zoom scenario: v00 is open (fit), its full level is the one stop */
    case 211: chk(211, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && info.copy_ms != GI_NONE && info.land_ms != GI_NONE && z_sx >= 96 && z_sx <= 104 && z_sy >= 86 && z_sy <= 94 && info.zt_n >= 40u && info.zt_dec_n >= 40u && info.zt_land_sum > 0u && info.zt_dec_sum > 0u && info.zt_land_max >= info.zt_land_sum / info.zt_n && info.zt_dec_max >= info.zt_dec_sum / info.zt_dec_n && info.zt_pass_max > 0u
               && info.zt_sp + info.zt_sc + info.zt_sn + info.zt_sj + info.zt_ss <= info.zt_pass_max && reads_while_decoding >= 30u && info.zt_n >= info.zt_dec_n && info.zt_n <= info.zt_dec_n + 1u && info.zt_dec_sum / info.zt_dec_n >= 200u); break;                 /* the pipeline: nearly every tile's stream was read while the previous tile decoded, one read a tile (at most one over), and the decode time is the decoder's own counter (62 SDRAM clocks a byte plus 20,000 in the model) */                                                                          /* A: the 1:1 view, decoded tile by tile into the window, equals the reference decode of the full level */
    case 212: chk(212, zk == 0 && z_shown && z_sx > 0 && z_sy > 0 && v1_view_matches("v00_full.rgb", 1000u, 900u) && info.zt_catch > 0u && info.zt_moved > 0u && info.zt_moved <= info.zt_catch && info.zt_n > 40u); zt_n_212 = info.zt_n; break;                                                  /* panned right and down: still the reference pixels */
    case 213: chk(213, zk < 0 && vbase == GAL_PLANE_BASE && plane_is_rgb("v00.rgb")); break;                                                                                /* A: back to fit, the fit plane untouched */
    case 214: chk(214, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && v1z_idx_ok && info.zt_n == zt_n_212 && info.zt_catch == 0u); break;                                                            /* zoom again: the same, the index kept */
    case 215: chk(215, zn == 1u && zk < 0 && vid_bus == 1u && h4[18] == 1u); break;                                                                                         /* the rotated picture (EXIF 180 degrees): its zoom level is written upright, the header's orientation is 1, so it has its zoom stop */
    case 216: chk(216, zk == 0 && z_shown && v1_view_matches("v06_full.rgb", 1000u, 900u)); break;                                                                          /* A zooms: the upright 1:1 level equals the reference decode of that level */
    case 217: force_ovf = 1; clear_texts(); break;                                                                                                                         /* the landing path counts an overflow: the first tile cannot be read */
    case 218: chk(218, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); force_ovf = 0; clear_texts(); break;                                                       /* the zoom is left, the grid is back with the message */
    case 219: v1z_cap = 600u; v1c_bs = 256u; v1c_n = 2u; v1c_reads = 0u; break;                                                                                                                                         /* an index of 768 bytes (64 tiles) no longer fits the buffer */
    case 220: pic_data[v1z.idx_off + 7u] ^= 1u; clear_texts(); chk(220, zn == 1u && zk < 0 && vid_bus == 1u && !v1z_idx_ok); break;                                                                      /* the index (768 bytes) no longer fits the buffer as far as this test is concerned: the picture still has its zoom stop */
    case 246: chk(246, said("ZOOM INDEX READ FAILED") && zk < 0 && !v1z_idx_ok); pic_data[v1z.idx_off + 7u] ^= 1u; break;      /* a flipped bit in an index kept on the card: the checksum, read in pieces, refuses it; the fit view stays */
    case 290: chk(290, sel == 8u && pic.rx == 0u && pic.ry == 226u && pic.rw == 800u && pic.rh == 267u && pic.width == 3600u); break;                                              /* the 3600 x 1200 panorama: its own rectangle on the fit canvas, not the whole canvas */
    case 291: { chk(291, zk == 0 && z_shown && z_stop.w == 3600 && z_stop.h == 1200 && abs(z_sx - 1400) <= 8 && abs(z_sy - 240) <= 8); } break;                                                        /* the zoom keeps the fit view's centre: the middle of the picture (level 1800, 600), so the corner is about (1400, 240) (the fit canvas's centre is 0.5 pixel off the picture's); with the canvas as the picture it started at the bottom (480) */
    case 292: { const double sc = 800.0 / 3600.0; const int32_t ex = (int32_t)floor(z_sx * sc / 4.0) * 4, ey = (int32_t)floor(226.0 + z_sy * sc + 0.5); chk(292, abs(m_rx - ex) <= 4 && abs(m_ry - ey) <= 2 && m_rw >= 160 && m_rw <= 180); } break;      /* the map rectangle is where the view is, on the picture */
    case 298: chk(298, sel == 10u && h4[18] == 3u && zn == 0u && zk < 0); break;                                                                                         /* a file with orientation 3 in the header (an older converter's): no zoom stop */
    case 299: chk(299, zk < 0 && !z_shown && vbase == GAL_PLANE_BASE); break;                                                                                             /* A does nothing there */
    case 296: poll_adv = 1111111u; break;                                                                                                                       /* a key poll is a frame: the map's held keys step */
    case 297: poll_adv = 0u; break;
    case 293: chk(293, m_ry >= 226 && m_ry + m_rh <= 226 + 267 && m_ry + m_rh >= 226 + 267 - 3); break;                                                                                /* pushed down it stops at the bottom edge of the PICTURE (y 493), not the canvas's (720) */
    case 294: chk(294, zk == 0 && z_shown && z_sy >= 470 && z_sy <= 480 && z_sy + 720 <= 1200); break;                                                                               /* the jump lands at the bottom of the level (corner y about 480) */
    case 295: chk(295, sel == 9u && pic.rx == 280u && pic.ry == 0u && pic.rw == 240u && pic.rh == 720u); break;                                                                        /* the 1100 x 3300 portrait: 240 x 720 at x 280 */
    case 244: chk(244, zk == 0 && z_shown && v1z_idx_ok && v1z.idx == 0 && v1z.fetch && v1c_reads > 6u && v1c_reads < 60u && v1_view_matches("v00_full.rgb", 1000u, 900u)); { /* the cache's replacement rule, driven directly: blocks A, B, A, C, A with two slots read 3 times from the card (least recently used out; a hit renews a block) */
        const uint32_t b0 = v1z.idx_off / 256u, A = (b0 + 1u) * 256u + 40u, B = (b0 + 2u) * 256u + 40u, C = v1z.idx_off; uint8_t e[12]; int okk = (v1z.idx_off % 256u) <= 240u;
        v1c_clock = 0u; for (uint32_t q = 0; q < 16u; q++) { v1c_pos[q] = 0xFFFFFFFFu; v1c_age[q] = 0u; } v1c_reads = 0u;
        const uint32_t seq[5] = { A, B, A, C, A }, want[5] = { 1u, 2u, 2u, 3u, 3u };
        for (int q = 0; q < 5; q++) { okk &= v1_idx_entry(0, seq[q], e) == 1; for (int k = 0; k < 12; k++) okk &= e[k] == pic_data[seq[q] + k]; okk &= v1c_reads == want[q]; }
        chk(247, okk); }
        break;      /* the zoom opened and shows the exact reference pixels with every tile's entry read through two 256 byte blocks (entries straddle blocks) */
    case 245: v1z_cap = V1Z_IDX_MAX; v1c_bs = 4096u; v1c_n = 8u; break;                                                                                  /* so that picture has no zoom stop */
    case 221: chk(221, zn == 1u && !v1z_idx_ok && zk < 0); fail_pic_reads = 3; pass_pic_reads = 0; clear_texts(); break;                                                      /* opened again with the normal buffer; the next card reads fail: the zoom's index cannot be read */
    case 222: chk(222, said("ZOOM INDEX READ FAILED") && zk < 0 && !v1z_idx_ok && vid_bus == 1u); fail_pic_reads = 0; clear_texts(); break;                                  /* the fit view stays, with the message */
    case 223: chk(223, zk == 0 && z_shown && v1z_idx_ok && v1_view_matches("v00_full.rgb", 1000u, 900u)); break;                                                            /* the card reads work again: the zoom opens */
    case 224: chk(224, zn == 0u && zk < 0 && vid_bus == 1u); break;                                                                                                         /* a 500 x 300 picture: its only level is no larger than the screen */
    case 230: clk_step = 1000000u; poll_adv = 0u; break;                                                                                                                                    /* from here a clock read is a frame: the pan's frame ticks happen */
    case 231: clk_step = 50u; poll_adv = 0u; break;
    case 232: { v1_pre_t p = { 1u, 0u, 0u, 3, 4 }; uint32_t o;                                                                                                              /* the prefetch bookkeeping, directly */
        chk(232, v1_pre_matches(&p, 3, 4) && !v1_pre_matches(&p, 3, 5) && !v1_pre_matches(&p, 4, 4) && (p.valid = 0u, !v1_pre_matches(&p, 3, 4)));                       /* only the tile it was read for, and only while valid */
        v1_pre.valid = 1u; v1_pre.buf = 0u; v1_pre.tx = 1; v1_pre.ty = 1; chk(233, !v1_land_stream(0, 0, 1u, &o) && v1_pre.valid == 1u);                                      /* a read into the other buffer leaves it */
        chk(234, !v1_land_stream(0, 0, 0u, &o) && v1_pre.valid == 0u); v1_pre.valid = 0u;                                                                              /* a read into its buffer takes it away */
        v1_pre.valid = 1u; v1_pre.buf = 1u; chk(235, v1_land_stream(0, 0, 1u, &o) == 0 && v1_pre.valid == 0u); v1_pre.valid = 0u; force_ovf = 1; v1_pre.valid = 1u; v1_pre.buf = 0u;
        chk(236, v1_land_stream(0, 0, 0u, &o) == 1 && v1_pre.valid == 0u); force_ovf = 0; v1_pre.valid = 0u; } break;                                                         /* even a failed read takes it away */
    case 250: chk(250, zn == 1u && zs[0].w == 2000u && zs[0].h == 1700u && zk < 0); break;                                                                                      /* a picture larger than the window: its zoom stop */
    case 270: poll_adv = 300000u; poll_alt = 3000000u; rs_f0 = zt_frames; rs_c0 = clk; rs_wx0 = z_wx; rs_catch0 = info.zt_catch; rs_moved0 = info.zt_moved; break;                                                                    /* from here the passes alternate 9 ms and 25 ms (about one frame on average) */
    case 272: { const uint32_t got = zt_frames - rs_f0; const int32_t d = rs_wx0 - z_wx;                                                       /* 10 polls of a held Left (a 128 pixel ring is resident, so nothing waits), the passes alternating 5 ms and 45 ms: the wanted corner moved 8 pixels for each frame that went by (4 for the first 8) and the shown view is on it; every frame behind was a frame moved (nothing to wait for) */
        chk(272, got >= 12u && d >= 8 * (int32_t)got - 36 && d <= 8 * (int32_t)got - 24 && z_sx == z_wx && info.zt_catch - rs_catch0 == info.zt_moved - rs_moved0 && info.zt_catch - rs_catch0 >= 12u); } break;
    case 273: poll_adv = CLK_HZ / 60u; poll_alt = 0u; rs_f0 = zt_frames; clk += 10u * (CLK_HZ / 60u); break;                                                    /* one pass of 10 frames, then passes of one frame */
    case 274: { const uint32_t got = zt_frames - rs_f0; chk(274, got >= 12u && got <= 15u); poll_adv = 0u; } break;                                                    /* the long pass is paid back by at most 4 frames; the rest is dropped (no burst afterwards): about 4 + 10 */
    case 271: { const uint32_t fr = (clk - rs_c0) / (CLK_HZ / 60u), got = zt_frames - rs_f0; chk(271, fr >= 250u && got + 4u >= fr && got <= fr + 3u); poll_adv = 0u; poll_alt = 0u; } break;      /* the pan frames follow the elapsed time (about 300 here), not the 200 passes */
    case 251: chk(251, info.zt_catch > 0u && info.zt_catch > info.zt_moved && info.zt_stall_run_max >= 1u && info.zt_stall_run_max <= info.zt_catch - info.zt_moved && info.zt_n >= info.zt_dec_n
               && info.zt_sj > 0u && info.zt_sn + info.zt_sc > 0u && info.zt_sp + info.zt_sc + info.zt_sn + info.zt_sj + info.zt_ss <= info.zt_pass_max && info.zt_pass_max - (info.zt_sp + info.zt_sc + info.zt_sn + info.zt_sj + info.zt_ss) <= 3u * (1000000u / 66u + 1u)); break;       /* the pass split adds up to the longest pass (the two clock reads that bound it are a frame apart in this model) */      /* a long pan outruns the tile reads: frames stalled, and the longest run of them is at least one and at most all of them */
    case 240: chk(240, said("ZOOM PIPELINE") && said("ON") && !said("OFF") && v1_pipe_off == 0u && said("A TOGGLE ZOOM PIPELINE")); clear_texts(); break;                                                      /* the Info page shows the switch, on */
    case 241: chk(241, said("OFF") && v1_pipe_off == 1u); rwd_241 = reads_while_decoding; clear_texts(); break;                                                                               /* A turned it off */
    case 242: chk(242, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && reads_while_decoding == rwd_241 && info.zt_n == info.zt_dec_n && info.zt_n >= 40u); break;             /* with the pipeline off: no card read while the decoder runs, one read a decode, the same pixels */
    case 243: chk(243, said("ON") && v1_pipe_off == 0u); clear_texts(); break;                                                                                                                /* and A turns it on again */
    case 226: v1z_smax = 100u; break;                                                                                                                                         /* no tile stream is allowed to be longer than 100 bytes */
    case 227: chk(227, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); v1z_smax = V1Z_STREAM_MAX; clear_texts(); break;                                             /* the first tile is refused: the zoom is left with the message */
    case 228: force_short = 1; clear_texts(); break;                                                                                                                         /* a landing of one word too few (counted by the landing path) */
    case 229: chk(229, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); force_short = 0; clear_texts(); break;                                                     /* is not accepted either */
    case 225: chk(225, land_unaligned == 0u); break;                                                                                                                         /* no landing read of a picture was unaligned (the tile streams read as 4 KB aligned supersets) */
    case 206: chk(206, said("NEEDS THE HW DECODER") && tg.starts == 0u); clear_texts(); break;                                                                                /* a core without the decoder */
    case 470: chk(470, hid_pairs() && rot_o == 6u && rot_plane_ok("v00.rgb", 6u, 800u, 720u) && vbase == GAL_ROT_BASE + 40u * GAL_LINE_WORDS16 && vstride == GAL_LINE_WORDS16 && !said("ERROR") && !said("FAILED")); break;      /* Y: turned 90 degrees clockwise: the plane holds the turned picture (720 wide, 800 tall, black margins), the view is centred on it */
    case 471: chk(471, rot_pan == 60u && vbase == GAL_ROT_BASE + 60u * GAL_LINE_WORDS16); break;                                     /* Down pans 20 rows */
    case 472: chk(472, rot_pan == 80u && vbase == GAL_ROT_BASE + 80u * GAL_LINE_WORDS16); break;                                     /* the pan stops at the last row */
    case 473: chk(473, rot_pan == 0u && vbase == GAL_ROT_BASE); break;                                                                 /* and at the first */
    case 474: chk(474, hid_pairs() && rot_o == 3u && rot_pan_max == 0u && rot_plane_ok("v00.rgb", 3u, 800u, 720u) && vbase == GAL_ROT_BASE); break;       /* Y again: 180 degrees, no pan needed */
    case 475: chk(475, hid_pairs() && rot_o == 6u && rot_plane_ok("v00.rgb", 6u, 800u, 720u) && vbase == GAL_ROT_BASE + 40u * GAL_LINE_WORDS16); break;   /* X: counter-clockwise from 180 is 90 clockwise; the margins are black again (the 180 picture was there) */
    case 476: chk(476, hid_pairs() && rot_o == 0u && vbase == GAL_PLANE_BASE && plane_is_rgb("v00.rgb")); break;                                    /* X: upright again, the upright plane is shown and is still the picture */
    case 477: chk(477, rot_o == 5u && rot_plane_ok("v00.rgb", 5u, 800u, 720u) && vbase == GAL_ROT_BASE + 40u * GAL_LINE_WORDS16); break;     /* X: 90 counter-clockwise; the details page and back keep the turned view */
    case 478: chk(478, zk >= 0 && rot_o == 0u && mask_i == 0u); break;                               /* A (zoom) on a turned picture: it goes upright and the 1:1 level is shown (the zoom views are upright) */
    case 479: chk(479, rot_o == 0u && vbase == GAL_PLANE_BASE); break;                                                                 /* the next picture is upright */
    case 480: chk(480, rot_o == 0u && vbase == GAL_PLANE_BASE && sel == 1u); break;                                                    /* a turned picture left with B and opened again from the grid is upright */
    case 500: chk(500, label_ok("MASK 1:1") && hid_pairs() && mask_i == 1u && mask_painted && plane_mask_ok("v00.rgb", 0u, 40u, 0u, 760u, 720u) && vbase == GAL_PLANE_BASE); break;                         /* L1: the square mask: the plane is the picture with everything outside the centred 720 x 720 black */
    case 501: chk(501, label_ok("MASK 4:5") && hid_pairs() && mask_i == 2u && mask_p == 0u && plane_mask_ok("v00.rgb", 0u, 0u, 40u, 800u, 680u)); break;                                                          /* L1: 4:5 in the picture's own (landscape) orientation is 5:4: 800 x 640; the picture shows again where the square mask had hidden it */
    case 502: chk(502, label_ok("MASK 4:5 TALL") && hid_pairs() && mask_i == 2u && mask_p == 1u && plane_mask_ok("v00.rgb", 0u, 112u, 0u, 688u, 720u)); break;                                                         /* Start: the mask turned: 4:5 portrait is 576 x 720 */
    case 503: chk(503, hid_pairs() && mask_i == 0u && !mask_painted && plane_is_rgb("v00.rgb") && vbase == GAL_PLANE_BASE); break;                                                        /* L1 until off: the whole picture again */
    case 504: chk(504, rot_o == 6u && mask_i == 1u && plane_mask_ok("v00.rgb", 6u, 40u, 40u, 760u, 760u)); break;                                                           /* a picture turned 90 degrees: the square mask is on the turned plane (720 wide, 800 tall: 40 rows black above and below) */
    case 505: chk(505, rot_o == 3u && mask_i == 0u && !mask_painted && plane_mask_ok("v00.rgb", 3u, 0u, 0u, 800u, 720u)); break;                                              /* turning the picture drops the mask */
    case 506: chk(506, rot_o == 0u && mask_i == 0u && vbase == GAL_PLANE_BASE); break;                                                                                      /* the next picture has no mask */
    case 507: chk(507, mask_i == 0u && zk >= 0 && rot_o == 0u); break;                                                                  /* A (zoom) with a mask on a turned picture: both are dropped, the upright 1:1 level is shown */
    case 509: chk(509, label_ok("MASK 4:5 TALL") && rot_o == 6u && mask_i == 2u && mask_p == 1u && plane_mask_ok("v00.rgb", 6u, 80u, 0u, 720u, 800u)); break;                                  /* L1 on the turned picture: 4:5 in the turned picture's own portrait orientation: 640 x 800 */
    case 510: chk(510, !lab_on && mask_i == 1u && mask_p == 0u && plane_mask_ok("v00.rgb", 0u, 40u, 0u, 760u, 720u)); break;                                                         /* Start on the square changes nothing */
    case 511: chk(511, mask_i == 0u && zk >= 0 && rot_o == 0u); break;                                                                 /* A (zoom) with a mask on the upright picture: the mask is dropped and the 1:1 level is shown */
    case 530: chk(530, vbase == GAL_PLANE_BASE && pic.rw > 100u && pic.rw < 800u + 1u && hs_starts >= 1u && !(hs_en & 1u)); clear_texts(); break;                       /* the viewer decoded picture 0 and counted it: the unit's origin was written per tile, counting is off again after the decode */
    case 531: dump_fb("hist_decoder"); chk(531, said("DETAILS  HISTOGRAMS") && said("2/3") && said("COUNTED FROM THE DECODED PICTURE") && !said("FROM THE THUMBNAIL") && hist_dec_ok() && hist_fine_screen_ok() && said("256") && said("LEVELS") && det_hist.n == (uint32_t)pic.rw * pic.rh); clear_texts(); break;
    case 532: chk(532, said("DETAILS  HISTOGRAMS") && said("2/3") && said("FROM THE THUMBNAIL") && !said("COUNTED FROM") && det_hist.n <= 96u * 72u); clear_texts(); break;     /* a picture that was not decoded in the viewer: the thumbnail's count, said so */
    case 533: chk(533, said("DETAILS  HISTOGRAMS") && said("COUNTED FROM THE DECODED PICTURE") && hist_dec_ok() && det_hist.n == (uint32_t)pic.rw * pic.rh && det_hist.n < 800u * 720u); clear_texts(); break;      /* v01 (500 x 300, black bars on the fit canvas): only the picture's rectangle was counted */
    case 534: chk(534, said("DETAILS  HISTOGRAMS") && said("FROM THE THUMBNAIL") && !said("COUNTED FROM") && sel == 1u); clear_texts(); break;      /* v01 was counted, then v11's decode counted 20 tiles and failed: the counters are not v01's any more, so its details fall back to the thumbnail */
    case 540: chk(540, hid_pairs() && rot_o == 1u && mask_i == 0u && rot_plane_ok("v00.rgb", 1u, 800u, 720u) && vbase == GAL_ROT_BASE); break;                                       /* Start + X: mirrored left-right (the view is the rotated plane, no pan needed) */
    case 541: chk(541, hid_pairs() && rot_o == 3u && rot_plane_ok("v00.rgb", 3u, 800u, 720u)); break;                                                                                 /* Start + Y from the mirror: top-bottom as well = turned 180 degrees */
    case 542: chk(542, hid_pairs() && rot_o == 1u && rot_plane_ok("v00.rgb", 1u, 800u, 720u)); break;                                                                                 /* Start + Y again: the flip undoes itself */
    case 543: chk(543, hid_pairs() && rot_o == 7u && rot_plane_ok("v00.rgb", 7u, 800u, 720u)); break;                                                                                 /* Y (a turn clockwise) of the mirrored picture is the anti-transposed one */
    case 544: chk(544, rot_o == 7u && rot_plane_ok("v00.rgb", 7u, 800u, 720u) && mask_i == 0u); break;                                                                  /* Start alone changes nothing without a mask */
    case 545: chk(545, rot_o == 4u && rot_plane_ok("v00.rgb", 4u, 800u, 720u)); break;                                                                                 /* four counter-clockwise turns leave the picture as it was; Start + Y flips it top-bottom */
    case 546: chk(546, mask_i == 1u && mask_painted && rot_o == 7u); break;                                                                                                /* L1: a crop mask on the turned picture */
    case 547: chk(547, rot_o == 5u && mask_i == 0u && !mask_painted && rot_plane_ok("v00.rgb", 5u, 800u, 720u)); break;                                                      /* Start + X: mirrored, and the mask is dropped (the plane is the whole picture again) */
    case 512: chk(512, mask_i == 1u && mask_painted && zk < 0 && zn == 0u); break;                                                                                         /* v01 has no zoom level: A with a mask on changes nothing, the mask stays */
    case 513: chk(513, mask_i == 0u && zk < 0 && rot_o == 0u && !mask_painted && plane_is_rgb("v00.rgb") && vbase == GAL_PLANE_BASE); break;                                /* back from the zoom to fit: the plane is the whole picture again, no black bands */
    case 514: chk(514, rot_o == 0u && zk < 0 && plane_is_rgb("v00.rgb") && vbase == GAL_PLANE_BASE); break;                                                                  /* back from the zoom to fit after a turn: the upright plane */
    case 570: starts_570 = tg.starts; if (getenv("APP_DEBUG")) printf("570: hist %d said-dec %d thumb %d dec_ok %d fine %d n %u pic %u hist_for %u sel %u v1on %u\n", said("DETAILS  HISTOGRAMS"), said("COUNTED FROM THE DECODED PICTURE"), said("THUMBNAIL"), hist_dec_ok(), hist_fine_screen_ok(), det_hist.n, (unsigned)pic.rw * pic.rh, hist_for, sel, v1_on);
              chk(570, said("DETAILS  HISTOGRAMS") && said("COUNTED FROM THE DECODED PICTURE") && !said("THUMBNAIL") && hist_dec_ok() && hist_fine_screen_ok() && det_hist.n == (uint32_t)pic.rw * pic.rh); clear_texts(); break;      /* Select on the grid, then Right: the picture was decoded for the page (the thumbnail is a crop of it), the decoder counted it */
    case 571: chk(571, said("COUNTED FROM THE DECODED PICTURE") && tg.starts == starts_570 && hist_dec_ok()); clear_texts(); break;      /* the same page opened again: the picture was counted already, it is not decoded a second time */
    case 565: chk(565, !lab_on && rot_o == 7u && rot_plane_ok("v00.rgb", 7u, 800u, 720u)); break;                                                                    /* the next key press (Start, which changes nothing) takes the bar off: the rows under it are back, the plane is the whole picture */
    case 580:
    chk(580, z2_vis_hits == 0u && z2_shown && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u) && z2_vx == ((int32_t)z2_s * (z_sx + 400) - z2_vw / 2) - (((int32_t)z2_s * (z_sx + 400) - z2_vw / 2) & 3) && tg.starts >= 4u); break;      /* R1 at the 1:1 view: the 2x view on the other window, centred where the 1:1 view's centre was, every pixel a 2 x 2 block (four decodes a tile) */
    case 581:
    z2_c0x = (z2_vx + z2_vw / 2) / (int32_t)z2_s; z2_c0y = (z2_vy + z2_vh / 2) / (int32_t)z2_s; chk(581, (z2_dbl == 2 || (z2_nrecentre >= 1u && z2_ncopy >= 5u && info.zt_moved > 0u)) && info.zt_dec_n >= 5u && info.zoom_ms != GI_NONE && info.zt_pass_max > 0u && z2_vis_hits == 0u && z2_shown && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u) && z2_vx > (int32_t)z2_s * (z_sx + 400) - z2_vw / 2 && z2_vy > (int32_t)z2_s * (z_sy + 360) - z2_vh / 2); break;      /* panned right and down: still the picture doubled, further along */
    case 582: chk(582, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && z_sx == ((z2_c1x - 400 < 0 ? 0 : z2_c1x - 400 > 200 ? 200 : z2_c1x - 400) & ~3) && z_sy == (z2_c1y - 360 < 0 ? 0 : z2_c1y - 360 > 180 ? 180 : z2_c1y - 360)); break;                                                                  /* L1: back to the 1:1 view, at the 2x view's centre (it had moved right and down) */
    case 593: nl593 = z2_zb_calls; chk(593, z2_shown && label_view_ok("2X ZOOM") && z2_zb_calls >= 2u && !zb_on && fail_pic_reads == 0u); break;                                                                                                         /* the 2x view is up and its bar says 2X ZOOM, on the last rows of the plane the screen reads; the loader's bar (LOADING 2X nn%) was written while it loaded */
    case 594: chk(594, z2_shown && z2_level == 4u && label_view_ok("4X ZOOM") && z2_zb_calls > nl593 && !zb_on); break;                                                                                                 /* the same for 4x: the loader's bar while the 4x plane loaded (the 2x view on the screen), then 4X ZOOM on the new plane */
    case 584: chk(584, z2_vis_hits == 0u && z2_level == 4u && z2_view_ok("v00_full.rgb", 1000u, 900u, 4u) && abs((z2_vx + z2_vw / 2) / (int32_t)z2_s - z2_c0x) <= 4 && abs((z2_vy + z2_vh / 2) / (int32_t)z2_s - z2_c0y) <= 4); break;      /* A again: the 4x view (every pixel 4 x 4), at the 2x view's centre */
    case 585: chk(585, z2_vis_hits == 0u && z2_level == 4u && z2_view_ok("v00_full.rgb", 1000u, 900u, 4u) && (z2_vx + z2_vw / 2) / (int32_t)z2_s < z2_c0x); break;                                                                  /* panned left and up at 4x: still exact, further along */
    case 600: fail_pic_reads = 2; pass_pic_reads = 0; break;                                                                                                                                           /* the card does not answer for two reads while the 2x level loads (a screenshot): the loader tries again, the view still comes up */
    case 601: dump_fb("hist_linear"); chk(601, hm_mode == 1u && said("LINEAR") && said("MEAN")); clear_texts(); break;                                                                                                          /* A on the histogram page: the next height scale (LINEAR), the page drawn again with the mean */
    case 603: chk(603, hm_mode == 2u && said("LOG")); clear_texts(); break;
    case 602: dump_fb("hist_overlay"); chk(602, hm_mode == 0u && said("DETAILS  HISTOGRAM OVERLAY") && said("A SCALE   LEFT RIGHT PAGE   B BACK") && said("GREEN") && said("SQRT")); clear_texts(); break;                                                                                           /* A back to SQRT and Right: the overlay page (its own title, the names under the plot) */
    case 595: chk(595, said("DETAILS  INFO") && vid_bus == 0u); clear_texts(); break;
    case 596: chk(596, said("DETAILS  HISTOGRAMS")); clear_texts(); break;
    case 597: chk(597, vid_bus == 1u && z2_shown && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u)); break;
    case 598: chk(598, said("DETAILS  INFO") && vid_bus == 0u); clear_texts(); break;
    case 599: chk(599, vid_bus == 1u && zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u)); break;
    case 590: chk(590, map_active == 1u && zk == 0 && z_shown && !z2_active && z2_hold == 1u && info.zt_dec_n > 0u && info.zt_catch > 0u); break;                                                                                  /* the 1:1 view with its map open; the Info page still has the 2x run's numbers */
    case 591: chk(591, map_active == 0u && zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u)); break;                                                                                     /* the map closed: the 1:1 view */
    case 592: chk(592, z2_shown && z2_level == 2u && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u)); break;                                                                                                       /* and A: the 2x view again */
    case 586: chk(586, z2_vis_hits == 0u && z2_level == 2u && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u)); break;                                                                                              /* L1: back to 2x */
    case 588: z2_c1x = (z2_vx + z2_vw / 2) / (int32_t)z2_s; z2_c1y = (z2_vy + z2_vh / 2) / (int32_t)z2_s; chk(588, z2_vis_hits == 0u && z2_level == 2u && z2_view_ok("v00_full.rgb", 1000u, 900u, 2u)); break;                                                                                              /* 4x again and B: 2x */
    case 589: chk(589, vid_bus == 0u && zk < 0); break;                                                                                                                                                     /* a held B in the 1:1 view: the grid */
    case 587: chk(587, vid_bus == 0u && zk < 0); break;                                                                                                                                                     /* a held B in the 2x view: the grid */
    case 583: chk(583, z2_hold == 0u && zk < 0 && vbase == GAL_PLANE_BASE && plane_is_rgb("v00.rgb")); break;                                                                                                /* A in the 2x view: the fit view */
    case 560: chk(560, label_ok("FILTER BRIGHT") && rot_o == 0u && gset.lut == 1u && pix_ctrl == 5u && pix_stores == 768u && pix_bad == 0u && pix_tables_are(1u)); break;                                  /* X: the next colour filter (BRIGHT): the tables are loaded and on, the picture is not turned */
    case 561: chk(561, label_ok("MIRRORED") && rot_o == 1u && gset.lut == 1u && pix_ctrl == 5u && pix_stores == 768u); break;                                                                     /* Start + X mirrors and leaves the filter alone */
    case 562: chk(562, label_ok("FILTER INVERT") && rot_o == 1u && gset.lut == 7u && pix_ctrl == 5u && pix_bad == 0u && pix_tables_are(7u)); break;                                                    /* six more: INVERT, and the mirror is still there */
    case 563: chk(563, label_ok("FILTER NORMAL") && gset.lut == 0u && pix_ctrl == 1u && pix_bad == 0u && rot_o == 1u); break;                                                                           /* one more wraps to NORMAL: the tables are off */
    case 564: chk(564, label_ok("ANTI-TRANSPOSED") && gset.lut == 0u && rot_o == 7u); break;                                                                                                               /* Y turns clockwise (the mirrored picture becomes the anti-transposed one, as case 543) and does not touch the filter */
    case 548: chk(548, rot_o == 0u && zk >= 0); break;                                                                                                                         /* R1 on a mirrored picture zooms too (upright) */
    case 508: chk(508, mask_i == 0u && sel == 0u); break;
    case 481: chk(481, rot_o == 0u && vbase == GAL_PLANE_BASE); break;                                                                 /* a version 0 picture cannot be turned: X and Y do nothing */
    case 300: chk(300, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "1/18")); break;                                                                               /* the details scenario: the grid, picture 1 selected */
    case 310: fail_idx = 1u; clear_texts(); break;
    case 311: chk(311, said("DETAILS  HISTOGRAMS") && count_text("NOT COMPUTED YET") == 5 && !said("FROM THE THUMBNAIL") && said("2/3")); fail_idx = 0u; clear_texts(); break;      /* the thumbnail cannot be read: the histograms page still opens, the frames say so */
    case 312: chk(312, thumbs_on_screen(0, 16u) && sel == 1u); break;
    case 309: chk(309, vid_bus == 1u && plane_is(3) && sel == 3u && !said("DETAILS")); break;                                                                  /* Select + Right together is the next picture, not the page */
    case 301: chk(301, said("DETAILS  INFO") && said("NAME") && said("Picture 00") && said("1 OF 18") && said("SIZE") && said("FILE") && !said("CAMERA") && !said("NOT READ YET") && said("1/3") && said("B OR SELECT BACK") && !thumbs_on_screen(0, 16u)); clear_texts(); break;      /* Select on the grid: the INFO page of the selected picture (a version 0 index has no details: four rows), two pages */
    case 313: chk(313, said("DETAILS  HISTOGRAMS") && said("2/3") && !said("NOT COMPUTED YET") && said("FROM THE THUMBNAIL") && hist_screen_ok(0u) && det_hist.n < 6912u && said("LUMA") && said("RED") && said("GREEN") && said("BLUE")); clear_texts(); break;      /* Right: the histograms page, counted from the thumbnail */
    case 302: chk(302, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "1/18") && !said("DETAILS")); clear_texts(); break;                                                                  /* B: the grid again, nothing changed */
    case 303: chk(303, said("DETAILS  INFO") && said("Picture 01") && said("2 OF 18") && !said("Picture 00")); clear_texts(); break;                                               /* Right, Select: the next picture's INFO page */
    case 314: chk(314, said("DETAILS  HISTOGRAMS") && hist_screen_ok(1u)); clear_texts(); break;
    case 304: chk(304, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "2/18") && sel == 1u); break;                                                                          /* Select closes it too */
    case 305: chk(305, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is(1) && land_ovf == 0u); break;                                                                      /* A: the picture view */
    case 306: chk(306, vid_bus == 0u && said("DETAILS  INFO") && said("Picture 01") && said("2 OF 18")); clear_texts(); break;                                                       /* Select in the picture view: the page replaces it (the viewer is off) */
    case 307: chk(307, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is(1) && land_ovf == 0u && !said("DETAILS")); break;                                                  /* B: the same picture is back on screen */
    case 308: chk(308, vid_bus == 1u && plane_is(2) && sel == 2u); clear_texts(); break;                                                                                                     /* and the view still steps to the next picture */
    case 1: chk(50, th_role[TR_TEXT_PRIMARY] == 0xFFFFu && th_role[TR_BG_BOTTOM] == 0u && text_mode == 0u); chk(1, thumbs_on_screen(0, count < 16u ? count : 16u)); snprintf(want, sizeof want, "1/%u", count); chk(2, said(want)); chk(3, said("TAU GALLERY") && !said("FAILED")); break;
    case 2: snprintf(want, sizeof want, "3/%u", count); chk(4, !strcmp(last_text(), want)); chk(20, said("Picture 02")); break;
    case 3: chk(5, vid_bus == 1u && vbase == GAL_PLANE_BASE && vstride == 1200u); chk(6, plane_is(2)); snprintf(path, sizeof path, "/Assets/tau_gallery/common/photos/p%02u.tgy", 2u); chk(7, !strcmp(last_open, path)); chk(8, land_ovf == 0u && land_words == 1728512u / 4u); break;
    case 4: chk(9, plane_is(3) && vid_bus == 1u); break;
    case 5: chk(10, plane_is(1)); break;
    case 6: chk(11, plane_is(count - 1u)); break;                                              /* Left from the first wraps to the last */
    case 7: { const uint32_t fp = (count - 1u) / 16u * 16u; chk(12, vid_bus == 0u); chk(13, thumbs_on_screen(fp, count - fp)); break; }      /* back in the grid after the wrap to the last picture: its page, redrawn after the viewer */
    case 8: { const uint32_t n = count - 1u >= 16u ? count - 1u - 16u : 0u; snprintf(want, sizeof want, "%u/%u", n + 1u, count); chk(14, !strcmp(last_text(), want)); chk(15, thumbs_on_screen(0, count < 16u ? count : 16u)); break; }   /* L1: a page back */
    case 9: snprintf(want, sizeof want, "%u/%u", count, count); chk(16, !strcmp(last_text(), want)); { const uint32_t fp = (count - 1u) / 16u * 16u; chk(17, thumbs_on_screen(fp, count - fp)); } break;    /* R1: the last picture, its page */
    case 10: hidden = 1; clear_texts(); break;
    case 11: chk(18, said("OPEN FAILED") && vid_bus == 0u); force_ovf = 1; clear_texts(); break;
    case 12: chk(19, said("PICTURE DAMAGED IN TRANSFER") && vid_bus == 0u); force_ovf = 0; force_short = 1; clear_texts(); break;
    case 13: snprintf(want, sizeof want, "%u/%u", count, count); chk(21, !strcmp(last_text(), want)); break;                       /* R1 from picture 3: fewer than a page ahead, so the last picture */
    case 14: chk(22, said("PICTURE DAMAGED IN TRANSFER") && vid_bus == 0u); force_short = 0; viewer_dead = 1; clear_texts(); break;     /* a lost word without an overflow is damage too */
    case 15: chk(23, said("VIEWER DID NOT START")); break;
    case 16: chk(24, said("CONTROLS") && said("HELP") && said("INFO") && said("A OPEN") && !thumbs_on_screen(0, count < 16u ? count : 16u)); clear_texts(); break;           /* Start: the menu replaces the grid */
    case 17: snprintf(want, sizeof want, "1/%u", (unsigned)GC_N); chk(25, said("OPEN THE PICTURE") && said(want) && !said("HELP")); clear_texts(); break;                                                           /* A on Controls: the list, its first row of 22 */
    case 18: chk(26, said("CONTROLS") && said("HELP") && !said("OPEN THE PICTURE")); clear_texts(); break;                                                       /* B: back in the menu */
    case 19: chk(27, said("ADD PHOTOS") && said("1/3") && !said("MENU")); clear_texts(); break;                                                                  /* Down, A: Help page 1 of 3 */
    case 20: chk(28, said("WHERE THINGS ARE") && said("2/3") && !said("ADD PHOTOS")); clear_texts(); break;                                                      /* Down turns the page */
    case 22: chk(30, said("INFO") && said("4D50331B") && said("800 X 720") && said("NONE YET") && said("LAST ERROR") && said("L1 LOADED 1 SAVED 0")); snprintf(want, sizeof want, "%u", count); chk(31, said(want)); clear_texts(); break;    /* Info before any picture was opened */
    case 23: chk(32, said("4D50331B") && count_text("NONE YET") == 11 && said(" MS") && said("432128 WORDS, 0 LOST")); chk(33, said("NONE") && !said("FAILED")); chk(44, said("L1 LOADED 1 SAVED 1")); chk(125, ldr_opens > 0u && ldr_chunks > 0u && ldr_first_ok && ldr_mono && ldr_pct >= 62u && ldr_pct < 100u); clear_texts(); break;   /* Info after viewing pictures: the last landing */
    case 24: chk(34, said("OPEN FAILED") && said("0 WORDS, 0 LOST")); clear_texts(); break;                                                                          /* Info after a failed open: the error, and no landing counts carried over */
    case 25: chk(35, said("RESUME LAST PICTURE") && said("ON") && !said("OFF") && said("GRID SIZE") && said("4 X 3") && said("DITHER") && said("ORDERED")); clear_texts(); break;                                                                    /* Settings page, resume on (the default) */
    case 26: chk(36, said("OFF") && (setw[2] & 1u) == 0u && set_writes == 0); clear_texts(); break;                                                                   /* toggled off: shown, but not yet written */
    case 27: chk(37, (setw[2] & 1u) == 0u && set_writes == 0); clk += 2u * CLK_HZ; break;                                                                             /* back in the grid, a key a moment ago: nothing published yet; now a quiet 2 s */
    case 28: chk(38, setw[2] == 0x4000005Fu && setw[0] == (1u << 27) && setw[1] == 0x40000001u && set_writes == 1); break;                                              /* published: only word 2 changed */
    case 29: { const char *p = "Assets/tau_gallery/common/photos/p17.tgy"; chk(39, setw[6] == count - 1u && setw[5] == (gf_crc32((const uint8_t *)p, (uint32_t)strlen(p)) & 0x7FFFFFFFu)); chk(40, (setw[0] >> 27) == 1u && setw[2] == 0x4000005Fu); } break;   /* the last picture the viewer showed: the last one (the open at CP(14), whose landing worked but whose viewer did not start, is not remembered) */
    case 30: clk += 2u * CLK_HZ; break;
    case 31: force_ovf = 1; break;                                                                                                                                       /* the next open fails: the last picture must stay the one before */
    case 32: chk(45, said("THEME") && said("TAU") && said("DARK") && said("WHITE") && said("LEFT RIGHT CHANGE")); clear_texts(); break;                                          /* the Theme page: the defaults */
    case 33: chk(46, said("LIGHT") && th_pol == 1u && text_mode == 1u && th_role[TR_BG_BOTTOM] == 0xEF9Du); clear_texts(); break;                                         /* Down, Right: light mode applied at once, and the text weights asked for */
    case 34: chk(47, said("OCEAN") && said("GLOW") && th_theme == 1u && th_pal == 2u && th_role[TR_ACCENT] == tc_accent(0x2A83u, 1u)); clear_texts(); break;              /* the theme and the accent changed; on a light theme a bright accent is scaled down */
    case 35: { const uint32_t x = 399u; chk(48, thumbs_on_screen(0, count < 16u ? count : 16u) && fb[100u * 512u + x] == th_role[TR_BG_BOTTOM] && fb[100u * 512u + x] != 0u); } break;   /* back in the grid: the background is the theme's, the frame its accent */
    case 36: chk(49, setw[1] == (0x40000000u | 2u | (1u << 5) | (1u << 11)) && setw[0] == (1u << 27)); break;                                                             /* the look is saved after a quiet second, as word 1 only */
    case 60: chk(60, big_grid_ok()); break;
    case 260: chk(260, sel == 3u && big_grid_ok()); break;                                                                                                     /* a new Down on the last picture (299, column 3 of the last row): the same column of the first row, 3 */
    case 261: chk(261, sel == count - 1u && big_grid_ok()); break;
    case 262: chk(262, sel == 0u && big_grid_ok()); break;
    case 263: chk(263, sel == count - 1u && big_grid_ok()); break;
    case 280: rs_sel0 = sel; break;
    case 281: chk(281, sel >= rs_sel0 + 7u * 16u || sel == count - 1u); break;
    case 269: chk(269, sel == 0u && big_grid_ok()); break;
    case 264: poll_adv = CLK_HZ / 20u; break;                                                                                             /* a key poll is 50 ms from here */
    case 265: chk(265, sel >= 12u && sel <= 60u && big_grid_ok()); break;                                                                /* about 30 repeats after the wrap: forward from 0, far from the end */
    case 266: poll_adv = 0u; break;
    case 267: poll_adv = CLK_HZ / 20u; break;
    case 268: chk(268, sel == count - 1u && big_grid_ok()); poll_adv = 0u; break;                                                         /* at the end, still held: it stayed on the last picture */
    case 61: chk(61, plane_is_f("b299.tgy") && sel == count - 1u); break;                                                                                                 /* A on the last picture of 300: the file of index 299 */
    case 62: chk(62, plane_is_f("b298.tgy") && sel == count - 2u && page == (count - 2u) / 16u); break;                                                                 /* Left in the viewer */
    case 63: chk(63, plane_is_f("b000.tgy") && sel == 0u && page == 0u); break;                                                                                         /* Right, Right: past the end to the first picture, across 18 pages */
    case 69: chk(69, sel == 0u && big_grid_ok()); break;
    case 70: chk(70, sel == 298u && big_grid_ok()); break;
    case 71: chk(71, sel == 2u && big_grid_ok()); break;                                                                                                          /* a new Down from 298 in the last row: the same column (2) of the first row */
    case 64: chk(64, plane_is_f("b150.tgy") && sel == 150u && page == 9u); break;                                                                                       /* a picture in the middle, opened from its page */
    case 72: fail_pic_reads = 1; pass_pic_reads = 0; clear_texts(); break;                                                                                              /* the header read fails once */
    case 73: chk(72, vid_bus == 1u && fail_pic_reads == 0 && plane_is(count - 1u) && !said("FAILED")); clear_texts(); break;                                                          /* the header is read again and the picture shows */
    case 75: fail_pic_reads = 1; pass_pic_reads = 1; clear_texts(); break;                                                                                               /* the header reads, the first landing chunk fails once */
    case 76: chk(74, vid_bus == 1u && fail_pic_reads == 0 && plane_is(count - 1u) && land_ovf == 0u && land_words == 1728512u / 4u && !said("FAILED")); clear_texts(); break;           /* the landing is repeated whole: the picture shows and the word count is exact, not doubled */
    case 77: fail_pic_reads = 2; pass_pic_reads = 1; clear_texts(); break;                                                                                               /* the landing fails twice in a row */
    case 74: chk(73, said("PICTURE READ FAILED") && vid_bus == 0u && fail_pic_reads == 0); clear_texts(); break;                                                                /* two failed reads: reported, the viewer stays off */
    case 81: chk(81, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is_f("z00.tgy")); break;                                                                          /* A on picture 1: the fit canvas, as before */
    case 82: clk_step = 50u; poll_adv = 1111111u; blit_cost = 20000u; zoom_max_len = 0u; zoom_max_rs = 0u; zoom_phase = 1; break;                                                                                 /* from here a clock read is a frame */
    case 83: { int32_t x = -1, y = -1; chk(83, view_matches("z00.tgy", &x, &y) && x == 600 && y == 390); chk(121, info.land_ms >= 50u && info.land_ms <= 100u && info.zoom_ms > 100u && info.copy_ms == GI_NONE); } break;      /* the raster path measured too: a 48 KB chunk, the first view, and no tile copy yet */                                                           /* A: the raster level of 2000 x 1500 lands; the screen opens on its middle, pixel exact */
    case 84: { int32_t x = -1, y = -1; chk(84, view_matches("z00.tgy", &x, &y) && x >= 724 && x <= 744 && (x & 3) == 0 && y == 390); } break;                                                /* about 20 frames held: 8 at 4 pixels, the rest at 8 (a held-count that never resets would give 900 or more) */                                 /* Right held: the view moved, aligned, exact */
    case 85: { int32_t x = -1, y = -1; chk(85, view_matches("z00.tgy", &x, &y) && x == 1200 && y == 390); } break;                                                           /* held on: it stops at the right edge */
    case 86: { int32_t x = -1, y = -1; chk(86, view_matches("z00.tgy", &x, &y) && x == 1200 && y >= 390 && y <= 780); } break;                                              /* Down held, loading behind: whatever is on screen is exactly the level */
    case 87: { int32_t x = -1, y = -1; chk(87, view_matches("z00.tgy", &x, &y) && x == 1200 && y == 780); chk(88, zoom_max_len > 0u && zoom_max_len <= 49152u); chk(89, zoom_max_rs <= 1u); } break;   /* the bottom; no card command over 48 KB; at most one command between two key polls */
    case 90: { int32_t x = -1, y = -1; chk(90, view_matches("z00.tgy", &x, &y) && y < 780); } break;                                                                           /* a reversal: still exact */
    case 91: { int32_t x = -1, y = -1; chk(91, view_matches("z00.tgy", &x, &y) && y == 0); } break;                                                                           /* up to the top edge */
    case 92: { int32_t x = -1, y = -1; chk(92, view_matches("z00.tgy", &x, &y) && x == 0 && y == 0); } break;                                                                 /* left to the left edge */
    case 93: chk(93, vid_bus == 1u && vbase == GAL_PLANE_BASE && vstride == 1200u && plane_is_f("z00.tgy")); break;                                                           /* A again: the fit canvas is back, untouched, in one write */
    case 94: chk(94, vid_bus == 1u && vbase == GAL_PLANE_BASE); break;                                                                                                        /* B in the zoom: back to fit, not to the grid */
    case 95: chk(95, vid_bus == 0u); break;                                                                                                                                   /* B again: the grid */
    case 96: chk(96, sel == 1u && vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is_f("z01.tgy")); break;                                                                  /* Select + Right while zoomed: the next picture, in fit */
    case 97: chk(97, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is_f("z02.tgy") && sel == 2u); break;                                                                  /* a picture with no zoom level: A changes nothing */
    case 98: { int32_t x = -1, y = -1; chk(98, view_matches("z03.tgy", &x, &y) && x == 0 && y == 390); } break;                                                              /* a level narrower than the screen (padded to 800): opens in the middle of its height */
    case 99: { int32_t x = -1, y = -1; chk(99, view_matches("z03.tgy", &x, &y) && x == 0 && y == 390); } break;                                                              /* Right held: nothing to pan */
    case 100: { int32_t x = -1, y = -1; chk(100, view_matches("z04.tgy", &x, &y) && x == 400 && y == 240); } break;                                                           /* the half level of a 3200 x 2400 picture (it also has a tile level, which A does not use) */
    case 108: { uint32_t rlo, rhi; gv_rows_loaded(zl.stride, zl.len, z_lo, z_hi, &rlo, &rhi); chk(103, rhi >= (uint32_t)z_wy + 720u + 24u); } break;                                         /* a pan down while the key is held: rows ahead of the screen are already loaded (35 here; a loader that only completed the screen would be within 8) */
    case 109: clk_step = 50u; poll_adv = 0u; blit_cost = 0u; break;                                                                                                                                         /* almost no time passes between key polls */
    case 110: { int32_t x = -1, y = -1; chk(104, view_matches("z00.tgy", &x, &y) && x == 0 && y == 0); } break;                                                              /* 20 polls with Right held and no frame elapsed: the view has not moved (a step is a frame, not a pass) */
    case 111: clk_step = 50u; poll_adv = 1111111u; blit_cost = 20000u; break;
    case 112: chk(105, fit_or_exact("z04.tgy")); break;                                                                                                                      /* Down held from the first instant, loading under way: fit plane or an exact zoom view, never unloaded rows */
    case 103: { int32_t x = -1, y = -1; chk(102, sel == 5u && tile_view_matches("z05.tgy", &x, &y) && x == 748 && y == 390); chk(106, blit_visible == 0u && blit_bad == 0u && tile_reads >= 40u && zoom_max_len <= 49152u && zoom_max_rs <= 1u && blit_max_pass > 0u && blit_max_pass <= 130u);      /* and at most 64 Talos commands between two key polls, plus the 60 that save or restore the loader bar's six rows (ten a row: a command is 127 halfwords at most, a few microseconds) */
     } break;      /* a picture whose only zoom level is tiles: A enters it; 48 tiles landed, copied by Talos into the window, never into the displayed area, no command over 48 KB */
    case 122: { int32_t x = -1, y = -1; chk(115, tile_view_matches("z04.tgy", &x, &y) && x == 1200 && y == 840); } break;                                                          /* A from the fit view takes the last stop: the tile 1:1 level, the centre kept (400, 300 of fit is 1,600, 1,200) */
    case 123: fail_pic_reads = 1; pass_pic_reads = 0; break;                                                                                                                 /* one tile read fails once */
    case 124: { int32_t x = -1, y = -1; chk(116, tile_view_matches("z05.tgy", &x, &y) && fail_pic_reads == 0); } break;                                                     /* the retry landed the tile: the view still comes up exact */
    case 128: { char a[40], b[40], c[40]; snprintf(a, sizeof a, "%u MS", info.zoom_ms); snprintf(b, sizeof b, "%u MS", info.land_ms); snprintf(c, sizeof c, "%u MS", info.copy_ms);
        chk(119, said("ZOOM OPEN") && said("ZOOM LAND") && said("ZOOM COPY") && said(a) && said(b) && said(c)); chk(120, info.zoom_ms != GI_NONE && info.zoom_ms > 100u && info.land_ms >= 50u && info.land_ms <= 100u && info.copy_ms >= 30u && info.copy_ms <= 150u); } break;      /* the Info page shows what the zoom measured: first view, a 48 KB land (57 ms of model card time plus clock reads), a tile copy */
    case 125: fail_pic_reads = 2; pass_pic_reads = 0; clear_texts(); break;
    case 126: chk(117, said("PICTURE READ FAILED") && vid_bus == 0u && fail_pic_reads == 0); break;                                                                          /* a raster chunk that cannot be read: back to the grid with the message */
    case 127: chk(118, t_recentre == 0u && t_front == 0u && win_flips == 0u && rc_starts >= 1u && t_wx[0] == 384 && t_wy[0] == 128); break;                                                        /* a pan past the window margin and back: the re-centre that began was cancelled, the window stayed */
    case 121: zoom_max_len = 0u; zoom_max_rs = 0u; tile_reads = 0u; blit_visible = 0u; blit_bad = 0u; blit_from_win = 0u; win_flips = 0u; blit_max_pass = 0u; ph_stall_max = 0u; ph_jump_max = 0u; ph_axx = 0u; ph_axy = 0u; rc_starts = 0u; break;                                          /* the picture has just been opened (its fit canvas landed in 128 KB commands): count the zoom's own commands from here */
    case 113: { int32_t x = -1, y = -1; chk(107, tile_view_matches("z04.tgy", &x, &y) && x == 1200 && y == 1320); } break;                                                          /* R1 from the raster half: the tile 1:1 level, the screen centre kept (800, 840 becomes 1,600, 1,680) */
    case 114: { int32_t x = -1, y = -1; chk(108, view_matches("z04.tgy", &x, &y) && x == 400 && y == 480); } break;                                                                /* L1: back to the raster half, the centre kept */
    case 115: chk(133, zbp_rmax >= 1u); chk(109, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z04.tgy")); break;                                                                                  /* L1 again: the fit view */
    case 117: { int32_t x = -1, y = -1; const uint32_t f0 = win_flips; (void)f0; chk(110, tile_view_matches("z05.tgy", &x, &y) && x == 1500 && t_recentre == 0u); chk(111, win_flips >= 1u && blit_from_win > 0u && blit_visible == 0u && blit_bad == 0u); chk(122, ph_jump_max <= 16u && ph_stall_max <= 300u); chk(123, info.flip_ms != GI_NONE && info.flip_ms > 0u && info.flip_ms < 20000u); } break;      /* a long held pan: the shown view never moves more than 16 pixels in a pass and never stands still for more than a window's worth of tile copies (about 250 passes, bound 300) while the keys are ahead of it: it follows at the card's pace instead of freezing until the whole way is loaded */   /* panned to the right edge across window re-centres: exact, flipped windows, tiles reused by Talos copies, nothing written into the screen */
    case 129: chk(124, ph_axx > 0u && ph_axy > 0u && ph_jump_max <= 16u); break;      /* a held diagonal pan: when the diagonal step's tiles are not whole the view still steps along one axis, each way */
    case 130: chk(126, rc_retarget >= 1u && ph_jump_max <= 16u); chk(127, rc_lead_bad == 0u); break;      /* right to a window edge, then down while the new window is composing: it is aimed again at where the view is now */
    case 131: zb_ok_phase = 1; to_n = to_bad = 0u; rc_lead_n = rc_lead_bad = 0u; zbp_n = zbp_max = zbp_prev = zbp_bad = 0u; break;
    case 132: chk(136, to_n > 100u && to_bad == 0u); chk(135, plane_is_f("z06.tgy")); chk(128, rc_lead_n >= 2u && rc_lead_bad == 0u && blit_visible == 0u); break;      /* z06: the windows composed for a pan lie where the pan needs them */
    case 133:
    chk(134, plane_is_f("z06.tgy")); chk(129, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z06.tgy") && zbp_n >= 1u && zbp_bad == 0u && zbp_max >= 1u); break;      /* the zoom's loader bar was on the fit plane while the first view loaded, and the plane is the picture again */
    case 136: chk(130, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z06.tgy")); break;
    case 134: chk(131, sel == 5u && zk == -1); break;
    case 135: chk(132, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z05.tgy") && zbp_max >= 1u); break;
    case 140:
    { int32_t x = -1, y = -1; chk(137, tile_view_matches("z06.tgy", &x, &y) && zk == 1); map_x0 = x; map_y0 = y; } break;
    case 141: { int32_t okx = 0, oky = 0; (void)okx; (void)oky; const double sf = (double)pic.rw / pic.width, sk = 1.0;      /* the rectangle is where the screen is, worked out here with doubles */
        const double ew = 800.0 * sf / sk, eh = 720.0 * sf / sk, ex = pic.rx + map_x0 * sf / sk, ey = pic.ry + map_y0 * sf / sk;
        chk(138, vbase == GAL_PLANE_BASE && fabs(m_rw - ew) <= 2.5 && fabs(m_rh - eh) <= 2.5 && fabs(m_rx - ex) <= 5.0 && fabs(m_ry - ey) <= 2.5 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); map_rx0 = m_rx; map_ry0 = m_ry; map_rw1 = m_rw; } break;
    case 142: chk(139, m_rx > map_rx0 + 20 && m_ry > map_ry0 + 10 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); break;      /* moved right and down: the frame is at the new place and the old place is the picture again */
    case 143: { int32_t x = -1, y = -1; chk(140, plane_is_f("z06.tgy") && zk == 1 && tile_view_matches("z06.tgy", &x, &y) && x == map_x0 && y == map_y0); } break;      /* B: the same view again, the plane without a frame */
    case 149: map_cx = m_rx + m_rw / 2; map_cy = m_ry + m_rh / 2; break;
    case 144: chk(141, m_rw > 1.8 * map_rw1 && m_rw < 2.2 * map_rw1 && abs((m_rx + m_rw / 2) - map_cx) <= 8 && abs((m_ry + m_rh / 2) - map_cy) <= 8 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); break;      /* L1: the half stop's screen is twice the size */
    case 145: { const double sf = (double)pic.rw / pic.width, sk = 0.5; int32_t okx, oky; zoom_off(0, &okx, &oky);
        map_ex = (int32_t)((m_rx - (int32_t)pic.rx) / sf * sk + okx); map_ey = (int32_t)((m_ry - (int32_t)pic.ry) / sf * sk + oky); chk(142, plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); map_rw2 = m_rw; } break;
    case 146: { int32_t x = -1, y = -1; chk(143, zk == 0 && view_matches("z06.tgy", &x, &y) && abs(x - map_ex) <= 12 && abs(y - map_ey) <= 12 && plane_is_f("z06.tgy")); } break;      /* A: the half stop opened where the frame was, and nothing of the frame is left in the fit plane */
    case 147: chk(144, map_runs == 3u && !z_shown && zk >= 0); break;      /* START while the first view is still loading does not open the map (three scripted STARTs opened it before) */
    case 148: map_rx1 = m_rx; break;
    case 154: chk(145, m_rx > map_rx1 && m_rx - map_rx1 <= 16); break;      /* released and pressed again: the speed starts from 4 again, not from where it was */
    case 150: chk(146, m_ry + m_rh == (int32_t)(pic.ry + pic.rh) && m_rx + m_rw <= (int32_t)(pic.rx + pic.rw) && m_rx + m_rw >= (int32_t)(pic.rx + pic.rw) - 3 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); break;      /* held to the bottom right: the rectangle stops at the picture's edge */
    case 151: chk(147, m_ry == (int32_t)pic.ry && m_rx >= (int32_t)pic.rx && m_rx <= (int32_t)pic.rx + 3 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); break;
    case 152: { const double sf = (double)pic.rw / pic.width; map_ex = (int32_t)((m_rx - (int32_t)pic.rx) / sf); map_ey = (int32_t)((m_ry - (int32_t)pic.ry) / sf); chk(148, m_rw < 0.6 * map_rw2 && m_rw > 0.4 * map_rw2 && plane_outline_is("z06.tgy", m_rx, m_ry, m_rw, m_rh)); } break;      /* R1 from the half stop: the 1:1 screen is half the size */
    case 153: { int32_t x = -1, y = -1; chk(149, zk == 1 && tile_view_matches("z06.tgy", &x, &y) && abs(x - map_ex) <= 12 && abs(y - map_ey) <= 12 && plane_is_f("z06.tgy")); } break;
    case 118: chk(112, fit_or_exact_tile("z05.tgy") && blit_visible == 0u); break;                                                                                           /* while loading under a held pan: the fit plane or an exact view, never an unloaded tile */
    case 119: { int32_t x = -1, y = -1; chk(113, tile_view_matches("z05.tgy", &x, &y) && y == 780 && info.zt_pass_max == 0u && blit_visible == 0u && ph_jump_max <= 16u && ph_stall_max <= 300u); } break;                                          /* down to the bottom edge */
    case 120: { int32_t x = -1, y = -1; chk(114, tile_view_matches("z05.tgy", &x, &y) && x == 0 && y == 0 && blit_visible == 0u && blit_bad == 0u); rc_retarget = 0u; } break;                   /* back to the top-left corner */
    case 101: fail_pic_reads = 2; pass_pic_reads = 0; clear_texts(); break;
    case 102: chk(101, said("PICTURE READ FAILED") && vid_bus == 0u && fail_pic_reads == 0); break;                                                                          /* the level cannot be read: back to the grid with the message */
    case 21: snprintf(want, sizeof want, "1/%u", count); chk(29, thumbs_on_screen(0, count < 16u ? count : 16u) && said(want) && said("TAU GALLERY")); break;   /* B, B: the grid again, thumbnails and footer redrawn */
    default: break;
    }
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    snprintf(dir, sizeof dir, "%s", argv[1]); char f[600]; snprintf(f, sizeof f, "%s/tau-library.tdb", dir);
    idx_data = slurp(f, &idx_len); if (!idx_data) return 2;
    count = (uint32_t)idx_data[8] | (uint32_t)idx_data[9] << 8;
    setw[0] = 1u << 27; setw[1] = 0x40000001u; setw[2] = 0x4000005Eu; setw[5] = 0x40000000u; setw[6] = 0x40000000u; setw[8] = 0u;      /* the package's defaults of the inherited variables: layout 1, appearance, word 2 with bit 0 clear (resume on), the player's marks in words 5 and 6 */
    if (argc > 2 && !strcmp(argv[2], "zoom")) {                                                      /* the zoom (option A): pan, clamp, loading behind, the loop rules */
        static uint32_t S4[32768]; int k = 0;
#define HOLD(key, n) do { for (int _i = 0; _i < (n); _i++) S4[k++] = (key); } while (0)
        S4[k++] = CP(80); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(81); S4[k++] = CP(82);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 130); S4[k++] = CP(83);
        HOLD(KEY_RIGHT, 20); HOLD(0, 3); S4[k++] = CP(84); HOLD(KEY_RIGHT, 100); HOLD(0, 3); S4[k++] = CP(85);
        HOLD(KEY_DOWN, 20); S4[k++] = CP(108); HOLD(KEY_DOWN, 40); HOLD(0, 3); S4[k++] = CP(86); HOLD(KEY_DOWN, 150); HOLD(0, 250); S4[k++] = CP(87);
        HOLD(KEY_UP, 40); HOLD(0, 2); S4[k++] = CP(90); HOLD(KEY_UP, 400); HOLD(0, 300); S4[k++] = CP(91);
        HOLD(KEY_LEFT, 200); HOLD(0, 5); S4[k++] = CP(92);
        S4[k++] = CP(109); HOLD(KEY_RIGHT, 20); S4[k++] = CP(110); S4[k++] = CP(111);
        S4[k++] = KEY_B; S4[k++] = 0; S4[k++] = CP(93);                                                                                                                                /* B: back to the fit view (A goes the other way now: the next zoom level in) */
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 130); S4[k++] = KEY_B; S4[k++] = 0; S4[k++] = CP(94); S4[k++] = KEY_B; S4[k++] = 0; S4[k++] = CP(95);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 130); S4[k++] = KEY_SELECT | KEY_RIGHT; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(96);
        S4[k++] = KEY_RIGHT; S4[k++] = 0; HOLD(0, 3); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 30); S4[k++] = CP(97);
        S4[k++] = KEY_RIGHT; S4[k++] = 0; HOLD(0, 3); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 130); S4[k++] = CP(98); HOLD(KEY_RIGHT, 30); HOLD(0, 3); S4[k++] = CP(99);
        S4[k++] = KEY_A; S4[k++] = 0; S4[k++] = KEY_RIGHT; S4[k++] = 0; HOLD(0, 3); S4[k++] = KEY_R1; S4[k++] = 0; HOLD(0, 110); S4[k++] = CP(100);
        S4[k++] = KEY_A; S4[k++] = 0; S4[k++] = KEY_R1; S4[k++] = 0; HOLD(KEY_DOWN, 45); for (int i = 0; i < 160; i++) { S4[k++] = 0; if (i % 8 == 7) S4[k++] = CP(112); }
        S4[k++] = KEY_R1; S4[k++] = 0; HOLD(0, 700); S4[k++] = CP(113);                                              /* the stop above: the tile 1:1 level of z04 */
        S4[k++] = KEY_L1; S4[k++] = 0; HOLD(0, 120); S4[k++] = CP(114); S4[k++] = KEY_L1; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(115);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 600); S4[k++] = CP(122); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3);
        S4[k++] = CP(125); S4[k++] = KEY_R1; S4[k++] = 0; HOLD(0, 12); S4[k++] = CP(126);                          /* the raster half of z04 cannot be read: the grid */
        S4[k++] = KEY_RIGHT; S4[k++] = 0; S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(121); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 450); S4[k++] = CP(103);   /* z05: open it from the grid; A takes its only stop, a tile level */
        HOLD(KEY_RIGHT, 40); HOLD(KEY_LEFT, 30); HOLD(0, 30); S4[k++] = CP(127);
        HOLD(KEY_RIGHT, 300); HOLD(0, 800); S4[k++] = CP(117);
        S4[k++] = KEY_A; S4[k++] = 0; S4[k++] = KEY_A; S4[k++] = 0; HOLD(KEY_DOWN, 60); for (int i = 0; i < 40; i++) { HOLD(0, 10); S4[k++] = CP(118); }
        HOLD(KEY_DOWN, 200); HOLD(0, 900); S4[k++] = CP(119);
        HOLD(KEY_UP, 400); HOLD(KEY_LEFT, 500); HOLD(0, 1200); S4[k++] = CP(120);
        HOLD(KEY_RIGHT, 200); HOLD(KEY_DOWN, 200); HOLD(KEY_RIGHT, 200); HOLD(0, 1500); S4[k++] = CP(130);
        HOLD(KEY_DOWN | KEY_RIGHT, 300); HOLD(0, 1500); S4[k++] = CP(129);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(123); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 450); S4[k++] = CP(124);
        S4[k++] = KEY_A; S4[k++] = 0; S4[k++] = CP(101); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 12); S4[k++] = CP(102);
        S4[k++] = KEY_RIGHT; S4[k++] = 0; S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(131); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 450);
        HOLD(KEY_RIGHT, 150); HOLD(0, 800); HOLD(KEY_DOWN, 150); HOLD(0, 800); HOLD(KEY_LEFT, 200); HOLD(0, 800); HOLD(KEY_RIGHT, 70); HOLD(0, 900); HOLD(KEY_DOWN, 70); HOLD(0, 900);
        HOLD(KEY_LEFT, 200); HOLD(KEY_UP, 200); HOLD(KEY_LEFT, 200); HOLD(0, 1200); HOLD(KEY_RIGHT, 200); HOLD(KEY_DOWN, 200); HOLD(KEY_RIGHT, 200); HOLD(0, 1200); HOLD(KEY_UP, 150); HOLD(KEY_LEFT, 150); HOLD(KEY_UP, 150); HOLD(0, 1200);
        S4[k++] = CP(132);
        S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 5); S4[k++] = CP(133);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 700); S4[k++] = CP(140);                                   /* the map: the 1:1 view of z06 is on screen */
        S4[k++] = KEY_START; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(141);
        HOLD(KEY_RIGHT, 30); HOLD(KEY_DOWN, 20); HOLD(0, 3); S4[k++] = CP(142); HOLD(0, 10); S4[k++] = CP(148); HOLD(KEY_RIGHT, 3); HOLD(0, 2); S4[k++] = CP(154);
        S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(143);
        S4[k++] = KEY_START; S4[k++] = 0; HOLD(0, 2); S4[k++] = CP(149); S4[k++] = KEY_L1; S4[k++] = 0; HOLD(0, 2); S4[k++] = CP(144);
        HOLD(KEY_DOWN, 300); HOLD(KEY_RIGHT, 300); HOLD(0, 2); S4[k++] = CP(150); HOLD(KEY_UP, 600); HOLD(KEY_LEFT, 600); HOLD(0, 2); S4[k++] = CP(151);
        HOLD(KEY_RIGHT, 20); HOLD(0, 3); S4[k++] = CP(145);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 700); S4[k++] = CP(146);
        S4[k++] = KEY_START; S4[k++] = 0; HOLD(0, 2); S4[k++] = KEY_R1; S4[k++] = 0; HOLD(0, 2); HOLD(KEY_DOWN, 30); HOLD(0, 2); S4[k++] = CP(152);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 700); S4[k++] = CP(153); S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 5);
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 2); S4[k++] = KEY_START; S4[k++] = 0; HOLD(0, 2); S4[k++] = CP(147); S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(136);      /* the zoom is left while its first view is loading: the loader bar is taken out of the fit plane */
        S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 2); S4[k++] = KEY_SELECT | KEY_LEFT; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(134); S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 700); S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 5); S4[k++] = CP(135);      /* another picture is opened while the zoom loads: its plane is its own, and the next zoom starts a fresh bar */      /* z06 3,600 x 2,400: windows that the level's edges do not clamp */
        S4[k++] = KEY_B; S4[k++] = 0; S4[k++] = KEY_B; S4[k++] = 0; HOLD(0, 12);
        S4[k++] = KEY_START; S4[k++] = 0; S4[k++] = KEY_DOWN; S4[k++] = 0; S4[k++] = KEY_DOWN; S4[k++] = 0; S4[k++] = KEY_A; S4[k++] = 0; HOLD(0, 3); S4[k++] = CP(128); S4[k++] = END;
        if (k > (int)(sizeof script / sizeof script[0])) { printf("FAIL the scenario script has %d steps, the script buffer holds %d (raise both arrays)\n", k, (int)(sizeof script / sizeof script[0])); return 2; }
        nscript = k; memcpy(script, S4, (size_t)k * sizeof S4[0]);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1hist")) {                                                            /* the histograms of a version 1 picture counted by the decoder's unit while the viewer decoded it (v00), and the thumbnail's for one that was not (v01) */
        caps_val = 0x80000007u;
        const uint32_t SH[] = { KEY_A, 0, KEY_B, 0, KEY_A, 0, CP(530), KEY_SELECT, 0, KEY_RIGHT, 0, CP(531), KEY_A, 0, CP(601), KEY_A, 0, CP(603), KEY_A, 0, KEY_RIGHT, 0, CP(602), KEY_LEFT, 0, KEY_B, 0, KEY_B, 0, KEY_RIGHT, 0, KEY_A, 0, KEY_SELECT, 0, KEY_RIGHT, 0, CP(533), KEY_B, 0, KEY_B, 0, KEY_RIGHT, 0, KEY_SELECT, 0, KEY_RIGHT, 0, CP(532), KEY_B, 0, KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, KEY_A, 0, KEY_B, 0, KEY_RIGHT, 0, KEY_RIGHT, 0, KEY_SELECT, 0, KEY_RIGHT, 0, CP(534), END };
        nscript = (int)(sizeof SH / sizeof SH[0]); memcpy(script, SH, sizeof SH);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "lib1details")) {                                                       /* the details pages of a version 1 library: every detail, only the format, none */
        lib1_on = 1;
        const uint32_t SX[] = { KEY_A, 0, CP(490), KEY_SELECT, 0, CP(491), KEY_RIGHT, 0, CP(492), KEY_RIGHT, 0, CP(493), KEY_RIGHT, 0, KEY_RIGHT, 0, CP(494), KEY_LEFT, 0, KEY_LEFT, 0, CP(495), KEY_B, 0, CP(496), KEY_RIGHT, 0, KEY_SELECT, 0, CP(497), KEY_RIGHT, 0, CP(498), KEY_B, 0, KEY_RIGHT, 0, KEY_SELECT, 0, CP(499), END };
        nscript = (int)(sizeof SX / sizeof SX[0]); memcpy(script, SX, sizeof SX);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "lib1light")) {                                                           /* the folder level in the light theme (its background is not 0, so the darkened strip shows) */
        lib1_on = 1; setw[1] = 0x40000000u | 2u | (1u << 5) | (1u << 11);
        const uint32_t SL[] = { CP(450), END }; nscript = 2; memcpy(script, SL, sizeof SL);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "colour")) {                                                                 /* Settings > COLOUR: presets load the tables (tables off while storing), NORMAL turns them off, Left wraps to INVERT */
        lib1_on = 1;
        const uint32_t SC[] = { CP(519), KEY_START, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_A, 0, CP(526), KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_RIGHT, 0, CP(520), KEY_UP, 0, CP(525), KEY_DOWN, 0, KEY_RIGHT, 0, CP(521), KEY_RIGHT, 0, KEY_RIGHT, 0, KEY_RIGHT, 0, CP(524), KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, CP(522), KEY_LEFT, 0, CP(523), KEY_B, 0, KEY_B, 0, END };
        nscript = (int)(sizeof SC / sizeof SC[0]); memcpy(script, SC, sizeof SC);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "libbigset")) {                                                           /* one folder of 60: select entry 48 (page 5 of 12), change the grid size in Settings, back in the grid */
        lib1_on = 1;
        const uint32_t SS[] = { KEY_R1, 0, KEY_R1, 0, KEY_R1, 0, KEY_R1, 0, KEY_START, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_A, 0, KEY_DOWN, 0, KEY_RIGHT, 0, KEY_B, 0, KEY_B, 0, CP(451), END };
        nscript = (int)(sizeof SS / sizeof SS[0]); memcpy(script, SS, sizeof SS);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && (!strncmp(argv[2], "lib1g", 5) || !strncmp(argv[2], "libbig", 6))) {                      /* a grid size of the setting (1..3) on the three-folder library (opened into the first folder) and on a library of one folder of 60 */
        lib1_on = 1; const int big = !strncmp(argv[2], "libbig", 6); gsel = (uint32_t)atoi(argv[2] + (big ? 6 : 5));
        setw[8] = gsel;                                                                                             /* the grid under test, dither raw 0 = ordered */
        if (big) { const uint32_t SB[] = { CP(440), KEY_R1, 0, CP(441), KEY_R1, 0, CP(442), END }; nscript = (int)(sizeof SB / sizeof SB[0]); memcpy(script, SB, sizeof SB); }
        else { const uint32_t SG[] = { KEY_A, 0, CP(430), KEY_DOWN, 0, CP(431), KEY_RIGHT, 0, CP(432), END }; nscript = (int)(sizeof SG / sizeof SG[0]); memcpy(script, SG, sizeof SG); }
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && (!strcmp(argv[2], "lib1") || !strncmp(argv[2], "lib1resume", 10))) {                      /* a version 1 library of three folders (20, 3 and 13 pictures) */
        lib1_on = 1;
        if (!strncmp(argv[2], "lib1resume", 10)) {
            const uint32_t ri = !strcmp(argv[2], "lib1resumeA") ? 5u : !strcmp(argv[2], "lib1resumeC") ? 30u : 21u; char rp[80]; snprintf(rp, sizeof rp, "Assets/tau_gallery/common/photos/l%02u.tgy", ri);
            setw[6] = ri; setw[5] = gf_crc32((const uint8_t *)rp, (uint32_t)strlen(rp)) & 0x7FFFFFFFu;
            const uint32_t SR[] = { CP(ri == 5u ? 421 : ri == 30u ? 422 : 420), END }; nscript = 2; memcpy(script, SR, sizeof SR);
        } else {
            const uint32_t S1[] = { CP(400), KEY_RIGHT, 0, CP(401), KEY_A, 0, CP(402), KEY_B, 0, CP(403), KEY_LEFT, 0, KEY_A, 0, CP(404), KEY_R1, 0, CP(405), KEY_R1, 0, CP(406), KEY_RIGHT, 0, CP(407), KEY_B, 0, CP(408),
                                    KEY_LEFT, 0, CP(409), KEY_A, 0, CP(410), KEY_B, 0, KEY_LEFT, 0, KEY_A, 0, KEY_A, 0, 0, 0, CP(411), KEY_LEFT, 0, 0, 0, CP(412), KEY_RIGHT, 0, 0, 0, CP(413), KEY_RIGHT, 0, 0, 0, KEY_B, 0, CP(414), END };
            nscript = (int)(sizeof S1 / sizeof S1[0]); memcpy(script, S1, sizeof S1);
        }
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "details")) {                                                       /* the picture details page: from the grid and from the picture view */
        const uint32_t SD[] = { CP(300), KEY_SELECT, 0, CP(301), KEY_RIGHT, 0, CP(313), KEY_B, 0, CP(302), KEY_RIGHT, 0, KEY_SELECT, 0, CP(303), KEY_RIGHT, 0, CP(314), KEY_SELECT, 0, CP(304), CP(310), KEY_SELECT, 0, KEY_RIGHT, 0, CP(311), KEY_B, 0, CP(312), KEY_A, 0, CP(305), KEY_SELECT, 0, CP(306), KEY_B, 0, CP(307), KEY_RIGHT, 0, CP(308), KEY_SELECT | KEY_RIGHT, 0, CP(309), END };
        nscript = (int)(sizeof SD / sizeof SD[0]); memcpy(script, SD, sizeof SD);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1zoom")) {                                                          /* zoom of a version 1 picture: its full level decoded by the hardware into the window */
        uint32_t S6[8192]; int k = 0;
#define H6(key, n) do { for (int _i = 0; _i < (n); _i++) S6[k++] = (key); } while (0)
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(210); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(211);
        S6[k++] = CP(230); H6(KEY_RIGHT, 40); H6(KEY_DOWN, 30); H6(0, 300); S6[k++] = CP(212); S6[k++] = CP(231); S6[k++] = CP(232);
        S6[k++] = KEY_A; S6[k++] = 0; H6(0, 3); S6[k++] = CP(213); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(214);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3);
        S6[k++] = KEY_START; S6[k++] = 0; S6[k++] = KEY_DOWN; S6[k++] = 0; S6[k++] = KEY_DOWN; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(240);                       /* the menu: Info, with the pipeline row */
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(241); S6[k++] = KEY_B; S6[k++] = 0; S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3);                                                         /* A on the Info page switches the pipeline off; back to the grid */
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(242); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3);   /* a zoom with the pipeline off */
        S6[k++] = KEY_START; S6[k++] = 0; S6[k++] = KEY_DOWN; S6[k++] = 0; S6[k++] = KEY_DOWN; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(243);
        S6[k++] = KEY_B; S6[k++] = 0; S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = CP(219);                                          /* back to fit, then to the grid; the index buffer too small */
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(220); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 5); S6[k++] = CP(246); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 250); S6[k++] = CP(244);                                  /* open v00: its zoom stop; the zoom works with the index on the card */
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = CP(245); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(221);                                                    /* open v00 again: a stop; then the index read fails */
        S6[k++] = KEY_A; S6[k++] = 0; H6(0, 20); S6[k++] = CP(222); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(223);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_RIGHT; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(224);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); for (int i = 0; i < 5; i++) { S6[k++] = KEY_RIGHT; S6[k++] = 0; } S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(215); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(216); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3);      /* the rotated picture is v06: it zooms now (its zoom level is written upright); B back to the fit view */
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); for (int i = 0; i < 6; i++) { S6[k++] = KEY_LEFT; S6[k++] = 0; } S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(217); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(218); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(226); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(227); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(228); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(229); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); for (int i = 0; i < 7; i++) { S6[k++] = KEY_RIGHT; S6[k++] = 0; } S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(250);
        S6[k++] = KEY_A; S6[k++] = 0; H6(0, 400); S6[k++] = CP(230); H6(KEY_RIGHT, 120); H6(0, 100); S6[k++] = CP(251); S6[k++] = CP(231); H6(0, 400); S6[k++] = CP(270); H6(KEY_LEFT, 10); S6[k++] = CP(272); H6(KEY_LEFT, 190); S6[k++] = CP(271); S6[k++] = CP(273); H6(0, 10); S6[k++] = CP(274); S6[k++] = CP(225); S6[k++] = END;
        if (k > (int)(sizeof script / sizeof script[0])) { printf("FAIL the scenario script has %d steps, the script buffer holds %d (raise both arrays)\n", k, (int)(sizeof script / sizeof script[0])); return 2; }
        nscript = k; memcpy(script, S6, (size_t)k * sizeof S6[0]);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1map")) {                                                           /* pictures that do not fill the fit canvas: a 3:1 panorama (v08) and a 1:3 portrait (v09): the picture's own rectangle drives the zoom entry and the map */
        uint32_t S7[4096]; int k = 0;
        S7[k++] = KEY_L1; S7[k++] = 0; for (int i = 0; i < 8; i++) { S7[k++] = KEY_RIGHT; S7[k++] = 0; } S7[k++] = KEY_A; S7[k++] = 0; S7[k++] = CP(290);                /* open v08 */
        S7[k++] = KEY_A; S7[k++] = 0; for (int i = 0; i < 400; i++) S7[k++] = 0; S7[k++] = CP(291);                                                                        /* zoom in: the first view */
        S7[k++] = KEY_START; S7[k++] = 0; S7[k++] = 0; S7[k++] = CP(292);                                                                                               /* the map */
        S7[k++] = CP(296); for (int i = 0; i < 80; i++) S7[k++] = KEY_DOWN; S7[k++] = 0; S7[k++] = CP(293); S7[k++] = CP(297);                                                                                  /* the rectangle pushed down as far as it goes */
        S7[k++] = KEY_A; S7[k++] = 0; for (int i = 0; i < 400; i++) S7[k++] = 0; S7[k++] = CP(294);                                                                        /* jump there */
        S7[k++] = KEY_B; S7[k++] = 0; S7[k++] = KEY_B; S7[k++] = 0; S7[k++] = KEY_RIGHT; S7[k++] = 0; S7[k++] = KEY_A; S7[k++] = 0; S7[k++] = CP(295);               /* back to the grid, v09, open */
        S7[k++] = KEY_B; S7[k++] = 0; S7[k++] = KEY_RIGHT; S7[k++] = 0; S7[k++] = KEY_A; S7[k++] = 0; S7[k++] = CP(298); S7[k++] = KEY_A; S7[k++] = 0; for (int i = 0; i < 20; i++) S7[k++] = 0; S7[k++] = CP(299);      /* v10: a file with the EXIF tag still in its header */
        S7[k++] = END;
        if (k > (int)(sizeof script / sizeof script[0])) { printf("FAIL the scenario script has %d steps, the script buffer holds %d (raise both arrays)\n", k, (int)(sizeof script / sizeof script[0])); return 2; }
        nscript = k; memcpy(script, S7, (size_t)k * sizeof S7[0]);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1mask")) {                                                           /* the crop masks on the fit view of a version 1 picture */
        const uint32_t SM[] = { KEY_A, 0, CP(200), KEY_L1, 0, CP(500), KEY_START, 0, CP(510), KEY_A, 0, CP(511), KEY_B, 0, CP(513), KEY_L1, 0, KEY_L1, 0, CP(501), KEY_START, 0, CP(502), KEY_L1, 0, KEY_L1, 0, KEY_L1, 0, KEY_L1, 0, KEY_L1, 0, KEY_L1, 0, CP(503),
                                KEY_Y, 0, KEY_L1, 0, CP(504), KEY_L1, 0, CP(509), KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, CP(505), KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_L1, 0, KEY_A, 0, CP(507), KEY_B, 0, KEY_RIGHT, 0, CP(506), KEY_L1, 0, KEY_A, 0, CP(512), END };
        nscript = (int)(sizeof SM / sizeof SM[0]); memcpy(script, SM, sizeof SM);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1mirror")) {                                                       /* the mirror keys (Start held + X or Y) on the fit view of a version 1 picture */
        const uint32_t SM[] = { KEY_A, 0, CP(200), KEY_START | KEY_X, 0, CP(540), KEY_START | KEY_Y, 0, CP(541), KEY_START | KEY_Y, 0, CP(542), KEY_Y, 0, CP(543), KEY_START, 0, CP(544), KEY_L1, 0, CP(546), KEY_START | KEY_X, 0, CP(547), KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_START | KEY_Y, 0, CP(545), KEY_R1, 0, CP(548), END };
        nscript = (int)(sizeof SM / sizeof SM[0]); memcpy(script, SM, sizeof SM);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1gridhist")) {                                                    /* the histograms page opened from the grid counts the decoded picture, not the cropped thumbnail */
        caps_val = 0x80000007u; hist_boost = 200u;                                                    /* the counts pass 65,535: the page scales them */
        const uint32_t SG[] = { KEY_SELECT, 0, KEY_RIGHT, 0, CP(570), KEY_B, 0, KEY_SELECT, 0, KEY_RIGHT, 0, CP(571), END };
        nscript = (int)(sizeof SG / sizeof SG[0]); memcpy(script, SG, sizeof SG);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && (!strcmp(argv[2], "v1zoom2") || !strcmp(argv[2], "v1zoom2d"))) {                                             /* v1zoom2d: the same on a core that doubles pixels at scan-out (VIEW_CAPS bit 3): 2x is the level's own pixels in a plane shown doubled, 4x is written twice over */
        if (!strcmp(argv[2], "v1zoom2d")) caps_val = 0x8000000Fu;                                                       /* the 2x and 4x zoom of a version 1 picture (v00: 1000 x 900): A goes a level in, B a level back, a long B leaves for the grid */
        uint32_t S2[8192]; int k = 0; legacy_a = 0u;
#define H2(key, n) do { for (int _i = 0; _i < (n); _i++) S2[k++] = (key); } while (0)
        S2[k++] = KEY_A; S2[k++] = 0; S2[k++] = CP(210); S2[k++] = KEY_A; S2[k++] = 0; S2[k++] = KEY_R1; S2[k++] = 0; H2(0, 200); S2[k++] = CP(211);                                          /* fit, then the 1:1 view (R1 before it shows is ignored) */
        S2[k++] = CP(231); S2[k++] = CP(600); S2[k++] = KEY_A; S2[k++] = 0; H2(0, 110); S2[k++] = CP(593); S2[k++] = CP(230); H2(0, 300); S2[k++] = CP(580);                                                                /* a slow clock while it loads: the bar's two seconds do not run out before the check */                                                                                                         /* A: the 2x view */
        H2(KEY_RIGHT, 40); H2(KEY_DOWN, 30); H2(0, 400); S2[k++] = CP(581);                                                                                                                       /* pan */
        S2[k++] = CP(231); S2[k++] = KEY_A; S2[k++] = 0; H2(0, 300); S2[k++] = CP(594); S2[k++] = CP(230); H2(0, 400); S2[k++] = CP(584);                                                                                                                               /* A again: the 4x view, the same centre */
        H2(KEY_LEFT, 60); H2(KEY_UP, 40); H2(0, 600); S2[k++] = CP(585);                                                                                                                           /* pan at 4x (the screen goes black when the plane is aimed again) */
        S2[k++] = KEY_L1; S2[k++] = 0; H2(0, 500); S2[k++] = CP(586);
        H2(KEY_RIGHT, 20); H2(0, 300);
        S2[k++] = KEY_SELECT; S2[k++] = 0; H2(0, 3); S2[k++] = CP(595); S2[k++] = KEY_RIGHT; S2[k++] = 0; S2[k++] = CP(596); S2[k++] = KEY_B; S2[k++] = 0; H2(0, 100); S2[k++] = CP(597);                          /* Select in the 2x view: the details; Right: the histograms; B: the 2x view again, as it was */
        S2[k++] = KEY_START; S2[k++] = 0; H2(0, 400); S2[k++] = CP(590); S2[k++] = KEY_B; S2[k++] = 0; H2(0, 100); S2[k++] = CP(591); S2[k++] = KEY_SELECT; S2[k++] = 0; H2(0, 3); S2[k++] = CP(598); S2[k++] = KEY_B; S2[k++] = 0; H2(0, 100); S2[k++] = CP(599); S2[k++] = KEY_A; S2[k++] = 0; H2(0, 500); S2[k++] = CP(592);                       /* Start in the 2x view: the 1:1 view's map to choose the area; B closes the map; A goes into 2x again */                                                                                                                              /* L1: back to 2x */
        S2[k++] = KEY_A; S2[k++] = 0; H2(0, 600); S2[k++] = KEY_B; S2[k++] = 0; H2(0, 500); S2[k++] = CP(588);                                                                                     /* 4x again, B: 2x */
        S2[k++] = KEY_B; S2[k++] = 0; H2(0, 200); S2[k++] = CP(582);                                                                                                                               /* B: the 1:1 view at the centre the zoom had */
        S2[k++] = KEY_B; S2[k++] = 0; H2(0, 5); S2[k++] = CP(583);                                                                                                                                 /* B: the fit view */
        S2[k++] = KEY_A; S2[k++] = 0; H2(0, 200); H2(KEY_B, 90); H2(0, 5); S2[k++] = CP(589); S2[k++] = KEY_A; S2[k++] = 0; H2(0, 3);                                                                /* the 1:1 view and a held B: the grid; the picture opened again */
        S2[k++] = KEY_A; S2[k++] = 0; H2(0, 200); S2[k++] = KEY_A; S2[k++] = 0; H2(0, 400); H2(KEY_B, 90); H2(0, 5); S2[k++] = CP(587); S2[k++] = END;                                              /* zoom, 2x, and a held B: the grid */
        nscript = k; memcpy(script, S2, (size_t)k * sizeof S2[0]);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1filter")) {                                                      /* X cycles the colour filters in the viewer, Start + X still mirrors, Y turns */
        const uint32_t SF[] = { KEY_A, 0, CP(200), KEY_X, 0, CP(560), KEY_START | KEY_X, 0, CP(561), KEY_X, 0, KEY_X, 0, KEY_X, 0, KEY_X, 0, KEY_X, 0, KEY_X, 0, CP(562), KEY_X, 0, CP(563), KEY_Y, 0, CP(564), KEY_START, 0, CP(565), END };
        nscript = (int)(sizeof SF / sizeof SF[0]); memcpy(script, SF, sizeof SF);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1rot")) {                                                           /* the turned view of a version 1 picture (v00: 1000 x 900, its fit canvas fills the screen) */
        const uint32_t SR[] = { KEY_A, 0, CP(200), KEY_Y, 0, CP(470), KEY_DOWN, 0, CP(471), KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, CP(472), KEY_UP, 0, KEY_UP, 0, KEY_UP, 0, KEY_UP, 0, KEY_UP, 0, KEY_UP, 0, CP(473), KEY_Y, 0, CP(474), KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, CP(475), KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, CP(476),
                                KEY_Y, 0, KEY_Y, 0, KEY_Y, 0, KEY_SELECT, 0, KEY_B, 0, CP(477), KEY_A, 0, CP(478), KEY_B, 0, CP(514), KEY_RIGHT, 0, CP(479), KEY_Y, 0, KEY_B, 0, KEY_A, 0, CP(480), KEY_RIGHT, 0, KEY_Y, 0, CP(481), END };
        nscript = (int)(sizeof SR / sizeof SR[0]); memcpy(script, SR, sizeof SR);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && (!strcmp(argv[2], "v1") || !strcmp(argv[2], "v1nohw"))) {                          /* version 1 pictures drawn by the hardware decoder, in a library that also has a version 0 picture */
        if (!strcmp(argv[2], "v1nohw")) { tgy_present = 0; const uint32_t S5[] = { KEY_A, 0, CP(206), END }; nscript = 4; memcpy(script, S5, sizeof S5); }
        else { const uint32_t S5[] = { KEY_A, 0, CP(200), KEY_RIGHT, 0, CP(201), KEY_RIGHT, 0, CP(202), KEY_RIGHT, 0, CP(203), KEY_RIGHT, 0, KEY_A, 0, CP(204), KEY_RIGHT, 0, KEY_A, 0, CP(205), KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, KEY_LEFT, 0, CP(207), KEY_A, 0, CP(208), END }; nscript = (int)(sizeof S5 / sizeof S5[0]); memcpy(script, S5, sizeof S5); }
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strncmp(argv[2], "big", 3)) {                                                   /* a large index (300 pictures): paging end to end, then the viewer across pages */
        setw[6] = 0x40000000u; int k = 0; uint32_t S3[1024];
        if (!strcmp(argv[2], "bigresume250") || !strcmp(argv[2], "bigresume299")) {
            const uint32_t idx = !strcmp(argv[2], "bigresume250") ? 250u : 299u; char pth[200]; snprintf(pth, sizeof pth, "Assets/tau_gallery/common/photos/b%03u.tgy", idx);
            setw[6] = idx; setw[5] = gf_crc32((const uint8_t *)pth, (uint32_t)strlen(pth)) & 0x7FFFFFFFu;
            S3[k++] = CP(60); S3[k++] = END;
        } else {
            const uint32_t pages = (count + 15u) / 16u;
            S3[k++] = CP(60); S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(60); S3[k++] = KEY_L1; S3[k++] = 0; S3[k++] = CP(69);      /* L1 on the first page goes to the first picture */
            for (uint32_t p = 0; p < pages; p++) { S3[k++] = KEY_R1; S3[k++] = 0; S3[k++] = CP(60); }                      /* R1 up to the last picture, one page at a time */
            S3[k++] = KEY_R1; S3[k++] = 0; S3[k++] = CP(60);                                                                         /* the last picture */
            S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(260); S3[k++] = KEY_UP; S3[k++] = 0; S3[k++] = CP(261);                                 /* a new Down on the last picture: the same column of the first row (3); a new Up there: the same column of the last row, the last picture */
            S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(262); S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = CP(263);                              /* the same for Right and Left */
            S3[k++] = CP(264); for (int i = 0; i < 6; i++) S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(269);                              /* Right held for 0.3 s: only the first press (to 0), no repeat yet */
            S3[k++] = KEY_RIGHT; for (int i = 0; i < 59; i++) S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(265);                                   /* Right held for 3 s (a poll is 50 ms): the first press wraps to 0, then it repeats forward */
            S3[k++] = CP(280); for (int i = 0; i < 8; i++) { S3[k++] = KEY_RIGHT | KEY_R1; S3[k++] = KEY_RIGHT; } S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(281);   /* R1 pressed while Right is held and a repeat is due: the R1 is not lost (the first pair presses Right as well: its R1 is dropped, as always; 7 pages) */
            S3[k++] = CP(266); for (int i = 0; i < 30; i++) { S3[k++] = KEY_R1; S3[k++] = 0; }                                                     /* back to the last picture by pages (the clock stands still again) */
            for (int i = 0; i < 4; i++) { S3[k++] = KEY_LEFT; S3[k++] = 0; }                                                                       /* 299 - 4 = 295 */
            S3[k++] = CP(267); for (int i = 0; i < 100; i++) S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(268);                                  /* Down held for 5 s: 295 -> 299, then it stops: a held key does not loop */
            S3[k++] = CP(266);
            S3[k++] = KEY_A; S3[k++] = 0; S3[k++] = CP(61); S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = CP(62);
            S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(63); S3[k++] = KEY_B; S3[k++] = 0; S3[k++] = CP(60);
            for (uint32_t p = 0; p < 10u; p++) { S3[k++] = KEY_R1; S3[k++] = 0; }                                         /* 0 -> 160 by pages */
            S3[k++] = CP(60); S3[k++] = KEY_UP; S3[k++] = 0; S3[k++] = KEY_UP; S3[k++] = 0; S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = CP(60);      /* 160 - 4 - 4 - 1 - 1 = 150, on page 9 */
            S3[k++] = KEY_A; S3[k++] = 0; S3[k++] = CP(64); S3[k++] = KEY_B; S3[k++] = 0; S3[k++] = CP(60);
            for (uint32_t p = 0; p < 9u; p++) { S3[k++] = KEY_R1; S3[k++] = 0; }                                          /* 150 -> 294 */
            S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(70); S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(71); S3[k++] = END;      /* a row down, then Down from the last row goes to the same column of the first row */
        }
        if (k > (int)(sizeof script / sizeof script[0])) { printf("FAIL the scenario script has %d steps, the script buffer holds %d (raise both arrays)\n", k, (int)(sizeof script / sizeof script[0])); return 2; }
        nscript = k; memcpy(script, S3, (size_t)k * sizeof S3[0]);
        if (!setjmp(done)) gallery_app_main();
        if (!strncmp(argv[2], "bigresume", 9)) { const uint32_t idx = setw[6]; chk(66, sel == idx && page == idx / 16u); chk(67, set_writes == 0); }
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2) {                                                                                     /* a boot scenario: the saved state, then the grid must open on the right picture */
        const char *sc = argv[2]; const uint32_t want = !strcmp(sc, "resume5") || !strcmp(sc, "ocean_light") || !strcmp(sc, "badlook") ? 5u : !strcmp(sc, "resume17") ? 17u : 0u;
        char pth[200]; const uint32_t idx = !strcmp(sc, "resume5") || !strcmp(sc, "badcrc") ? 5u : !strcmp(sc, "resume17") ? 17u : !strcmp(sc, "toohigh") ? count : 5u;
        snprintf(pth, sizeof pth, "Assets/tau_gallery/common/photos/p%02u.tgy", idx);
        setw[6] = idx; setw[5] = gf_crc32((const uint8_t *)pth, (uint32_t)strlen(pth)) & 0x7FFFFFFFu;
        if (!strcmp(sc, "badcrc")) setw[5] ^= 1u;
        if (!strcmp(sc, "off")) setw[2] |= 1u;
        if (!strcmp(sc, "layout0")) setw[0] = 0u;
        if (!strcmp(sc, "ocean_light")) setw[1] = 0x40000000u | 2u | (1u << 5) | (1u << 11);
        if (!strcmp(sc, "badlook")) setw[1] = 0x40000000u | 31u | (40u << 5);                      /* theme 40 and accent 31 do not exist in this build */
        const uint32_t S2[] = { CP(41), END }; nscript = 2; memcpy(script, S2, sizeof S2);
        if (!setjmp(done)) gallery_app_main();
        const uint32_t fp = want / 16u * 16u; char w[32]; snprintf(w, sizeof w, "%u/%u", want + 1u, count);
        chk(41, !strcmp(last_text(), w)); chk(42, thumbs_on_screen(fp, count - fp < 16u ? count - fp : 16u)); chk(43, set_writes == 0);
        if (!strcmp(sc, "ocean_light")) chk(51, th_theme == 1u && th_pol == 1u && th_pal == 2u && fb[100u * 512u + 399u] == th_role[TR_BG_BOTTOM] && fb[100u * 512u + 399u] != 0u && text_mode == 1u);
        else chk(52, th_theme == 0u && th_pol == 0u && th_pal == 1u && fb[100u * 512u + 399u] == 0u && text_mode == 0u);                     /* every other boot, and a look this build does not have, gives the default look */
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    const uint32_t S[] = { CP(1), KEY_START, 0, CP(16), KEY_A, 0, CP(17), KEY_B, 0, CP(18), KEY_DOWN, 0, KEY_A, 0, CP(19), KEY_DOWN, 0, CP(20), KEY_B, 0, KEY_DOWN, 0, KEY_A, 0, CP(22), KEY_B, 0, KEY_DOWN, 0, KEY_A, 0, CP(25), KEY_A, 0, CP(26), KEY_B, 0, KEY_B, 0, CP(27), 0, 0, CP(28), CP(21), KEY_RIGHT, 0, KEY_RIGHT, 0, CP(2), KEY_A, 0, CP(3), KEY_RIGHT, 0, CP(4), KEY_LEFT, 0, KEY_LEFT, 0, CP(5), KEY_LEFT, 0, KEY_LEFT, 0, CP(6), KEY_B, 0, CP(7), KEY_START, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_A, 0, CP(23), KEY_B, 0, KEY_B, 0, CP(72), KEY_A, 0, CP(73), KEY_B, 0, CP(75), KEY_A, 0, CP(76), KEY_B, 0, CP(77), KEY_A, 0, CP(74), KEY_A, 0, KEY_B, 0,
                          KEY_L1, 0, CP(8), KEY_R1, 0, CP(9), KEY_L1, 0, KEY_RIGHT, 0, KEY_R1, 0, CP(13), KEY_L1, 0, CP(10), KEY_A, 0, CP(11), KEY_START, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_A, 0, CP(24), KEY_B, 0, KEY_B, 0, KEY_RIGHT, 0, KEY_A, 0, CP(12),
                          KEY_RIGHT, 0, KEY_A, 0, CP(14), KEY_A, 0, CP(15), CP(31), KEY_RIGHT, 0, KEY_A, 0, CP(30), 0, 0, CP(29), KEY_START, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_DOWN, 0, KEY_A, 0, CP(32), KEY_DOWN, 0, KEY_RIGHT, 0, CP(33), KEY_DOWN, 0, KEY_RIGHT, 0, KEY_UP, 0, KEY_UP, 0, KEY_RIGHT, 0, CP(34), KEY_B, 0, KEY_B, 0, CP(35), CP(30), 0, 0, CP(36), END };
    nscript = (int)(sizeof S / sizeof S[0]); memcpy(script, S, sizeof S);
    if (!setjmp(done)) gallery_app_main();
    printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails);
    return fails ? 1 : 0;
}
