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
static uint32_t clk, irq_pend, vid_req, vid_bus, vid_sw, vid_due, vbase, vstride, land_base, land_words, land_ovf, input_n;
static int force_ovf, force_short, viewer_dead, fail_pic_reads, pass_pic_reads; static uint32_t v1_starts_before, land_unaligned;
static int zb_ok_phase = 0; static int32_t map_cx, map_cy, map_rx1, map_rw2, map_x0, map_y0, map_rx0, map_ry0, map_ex, map_ey, map_rw1;
static uint32_t rc_lead_n, rc_lead_bad; static uint32_t rc_prev, rc_starts, rc_retarget; static int32_t rc_bx, rc_by;
static void ldr_lead_probe(void);
static void ldr_probe(uint32_t dst);
static void tile_read_probe(uint32_t off);
static void zb_probe(void);
static void pass_hook(void);                                                                  /* called once per pass of the firmware's loop (a read of the keys): defined after the firmware, which it looks into */
static uint32_t blits_since_input, blit_max_pass; static uint32_t blit_cmds, blit_visible, blit_bad, blit_from_win, tile_reads, win_flips, last_win = 9u; static uint32_t clk_step = 50u, zoom_max_len, zoom_max_rs, reads_since_input; static int zoom_phase;      /* the zoom scenario: a frame a clock read, the longest card command and the most commands between two key polls */ static uint32_t text_mode;
static uint32_t setw[16], setidx; static int set_writes;                  /* the settings register file (R_SET_IDX / R_SET_DAT) and how many words were written */
static void tick(void) { if (!viewer_dead && vid_req != vid_bus && vid_due && clk >= vid_due) { vid_bus = vid_req; vid_sw++; vid_due = 0; } }
static uint32_t next_input(void);
static uint32_t tgy_rd(uint32_t a); static void tgy_wr(uint32_t a, uint32_t v); static int tgy_present = 1;       /* the model of the hardware TGY decoder (registers 0x220-0x244), after the firmware sources */
static uint32_t rd(uint32_t a)
{
    if (a >= 0x80000220u && a <= 0x80000244u) return tgy_rd(a);
    switch (a) {
    case 0x80000084u: return 0;
    case 0x800001ECu: return (vid_sw << 16) | (vid_req << 2) | vid_bus;
    case 0x800001F0u: return vbase;
    case 0x800001F4u: return vstride;
    case 0x80000208u: return 0x80000001u;
    case 0x8000020Cu: return land_words;
    case 0x80000210u: return force_ovf ? 1u : land_ovf;
    case 0x80000040u: return 0x4D50331Bu;
    case 0x800001F8u: return 0x80000003u;
    case 0x80000070u: return setw[setidx & 15u];
    case 0x80000114u: return 0x80000000u | text_mode;
    case 0x8000003Cu: reads_since_input = 0; blits_since_input = 0; pass_hook(); return next_input();
    default: return 0;
    }
}
static uint32_t cyc_rd(void) { clk += clk_step; tick(); return clk; }
static void wr(uint32_t a, uint32_t v)
{
    if (a >= 0x80000220u && a <= 0x80000244u) { tgy_wr(a, v); return; }
    switch (a) {
    case 0x80000114u: text_mode = v & 1u; break;
    case 0x8000006Cu: setidx = v; break;
    case 0x80000070u: setw[setidx & 15u] = v; set_writes++; break;
    case 0x800001ECu: vid_req = v & 1u; vid_due = clk + 2u * 1111111u; break;
    case 0x800001F0u: vbase = v & 0x1FFFFFFu; { const uint32_t wn = (vbase >= T_WIN1 && vbase < T_STAGE) ? 1u : (vbase >= T_WIN0 && vbase < T_WIN1) ? 0u : 9u; if (wn != 9u && wn != last_win) { if (last_win != 9u) win_flips++; last_win = wn; } } break;
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
static uint32_t ldr_chunks, ldr_max, ldr_prev, ldr_first_ok = 1u, ldr_mono = 1u, ldr_skip, ldr_opens;
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)to; reads++;
    if (zoom_phase && slot == 2u && len == 49152u) { tile_reads++; tile_read_probe(off); }
    if (!zoom_phase && slot == 2u && len == 131072u && dst >= 0x40000000u) ldr_probe(dst);
    if (zoom_phase && slot == 2u) { if (len > zoom_max_len) zoom_max_len = len; if (++reads_since_input > zoom_max_rs) zoom_max_rs = reads_since_input; }
    if (slot == 2u && (dst >> 28) == 4u && ((off & 4095u) || (len & 4095u))) land_unaligned++;      /* a landing read of a picture starts and ends on 4 KB (the APF reads fastest aligned; every landing in this firmware keeps to it) */
    if (slot == 2u) { if (pass_pic_reads > 0) pass_pic_reads--; else if (fail_pic_reads > 0) { fail_pic_reads--; return 0; } }      /* a transient card error: after `pass` good reads the next `fail` reads of a picture fail */
    const uint8_t *src = slot == 5u ? idx_data : slot == 2u ? pic_data : 0; const uint32_t n = slot == 5u ? idx_len : pic_len;
    if (!src || off + len > n || (len & 3u)) return 0;                                        /* a read past the end of the file fails, as on the Pocket */
    if ((dst >> 28) == 4u) {
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
static char texts[64][64]; static int ntext;
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { (void)x; (void)y; (void)fg; (void)bg; if (ntext == 64) ntext = 0; strncpy(texts[ntext], s, 63); texts[ntext++][63] = 0; return 0; }
uint32_t gfx_text_width(const char *s) { return 8u * (uint32_t)strlen(s); }
void irq_enable(uint32_t m) { (void)m; }
/* the Talos BLIT as the RTL has it (docs/M2_DESIGN.md section 9, sim/tb_talos_tilecopy.v): bases 25 bits, offsets 19 bits, a row at most 127 halfwords; this model also counts copies that read a window and any write into the displayed screen area */
void gfx_fence(void) { }
void gfx_blit_raw(uint32_t sb, uint32_t db, uint32_t so, uint32_t dof, uint32_t w)
{
    if (!w || w > 127u || so >= (1u << 19) || dof >= (1u << 19) || sb >= (1u << 25) || db >= (1u << 25)) { blit_bad++; return; }
    const uint32_t s = sb + so, d = db + dof; blit_cmds++; if (++blits_since_input > blit_max_pass) blit_max_pass = blits_since_input;
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
static uint32_t next_input(void)
{
    for (;;) {
        if (input_n >= (uint32_t)nscript) longjmp(done, 1);
        const uint32_t t = script[input_n++];
        if (t == END) longjmp(done, 1);
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
static struct { uint32_t src, len, dst, pitch, geom, qy, qc, status, polls; uint16_t ty[64], tc[64]; int tables_ok; uint32_t starts, loads; int corrupt_next; } tg;
static tgy_dec_t tg_dec; static tgy_planes_t tg_planes;
static uint8_t mbyte(uint32_t a) { return (uint8_t)(mem[a / 2u] >> (8u * (a & 1u))); }
static void msetb(uint32_t a, uint8_t v) { const uint32_t h = a / 2u, b = a & 1u; mem[h] = (uint16_t)((mem[h] & ~(0xFFu << (8u * b))) | (uint32_t)v << (8u * b)); }
typedef struct { uint32_t dst, pitch, w; } tg_emit_t;
static void tg_emit(void *c, unsigned row, const uint8_t *rgb) { tg_emit_t *e = c; for (uint32_t i = 0; i < e->w * 3u; i++) msetb(e->dst + row * e->pitch + i, rgb[i]); }
static uint32_t tgy_rd(uint32_t a)
{
    if (!tgy_present) return 0u;
    switch (a) {
    case 0x80000224u: { uint32_t s = tg.status & ~1u; if (tg.polls > 0) { tg.polls--; s |= 1u; } return 0x80000000u | s | (tg.tables_ok ? 8u : 0u); }
    case 0x80000244u: return 4242u;
    default: return 0u;
    }
}
static void tgy_wr(uint32_t a, uint32_t v)
{
    if (!tgy_present) return;
    switch (a) {
    case 0x80000228u: tg.src = v & 0x3FFFFFFu; break; case 0x8000022Cu: tg.len = v & 0xFFFFFu; break; case 0x80000230u: tg.dst = v & 0x3FFFFFFu; break; case 0x80000234u: tg.pitch = v & 0xFFFFu; break;
    case 0x80000238u: tg.geom = v & 0x3FFFFu; break; case 0x8000023Cu: tg.qy = v & 0x3FFFFFFu; break; case 0x80000240u: tg.qc = v & 0x3FFFFFFu; break;
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
            for (uint32_t i = 0; i < tg.len && i < sizeof sbuf; i++) sbuf[i] = mbyte(tg.src + i);                      /* the DMA's stream fetch from the landing window */
            tg_emit_t e = { tg.dst, tg.pitch, w };
            const int err = (fmt == 1u || !w || !h || w > 128u || h > 128u) ? 1 : tgy_tile_rgb(&tg_dec, &tg_planes, sbuf, tg.len, fmt, rows, cols, w, h, tg.ty, tg.tc, tg_emit, &e);
            tg.status = (tg.status & ~0x6u) | (err ? 4u : 2u) | (err ? (1u << 8) : 0u); tg.starts++; tg.polls = 3;
        }
        break;
    default: break;
    }
}
_Static_assert(T_WIN0 == GAL_WIN0 && T_WIN1 == GAL_WIN1 && T_STAGE == GAL_STAGE, "the model's window addresses are the firmware's");

static uint32_t count;
static const uint8_t *thumb_of(uint32_t i) { uint32_t atlas = (uint32_t)idx_data[20] | (uint32_t)idx_data[21] << 8 | (uint32_t)idx_data[22] << 16; return idx_data + atlas + i * 13824u; }
static int thumbs_on_screen(uint32_t first, uint32_t n)                                          /* every pixel of the page's thumbnails against the atlas */
{
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *t = thumb_of(first + k); const uint32_t x = 2u + (k % 4u) * 100u, y = 22u + (k / 4u) * 76u;
        for (uint32_t r = 0; r < 72u; r++) for (uint32_t c = 0; c < 96u; c++) { const uint32_t o = (r * 96u + c) * 2u; if (fb[(y + r) * 512u + x + c] != (uint16_t)(t[o] | t[o + 1] << 8)) return 0; }
        for (uint32_t c = 0; c < 96u; c++) { const uint16_t v = fb[(y + 72u) * 512u + x + c]; if (v != th_role[TR_BG_BOTTOM] && v != th_role[TR_ACCENT]) return 0; }          /* nothing written under the thumbnail: black or the selection frame */
    }
    return 1;
}
static int plane_is(uint32_t pic)                                                                 /* the plane equals the canvas of picture `pic` byte for byte */
{
    char f[600]; snprintf(f, sizeof f, "%s/p%02u.tgy", dir, pic); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) { const uint8_t got = (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))); if (got != d[20480u + b]) bad++; }
    free(d); return bad == 0u;
}
static int plane_is_rgb(const char *fname)                                                       /* the plane equals the 800 x 720 RGB888 file `fname` (the reference decode of a version 1 picture) */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d || n != 1728000u) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) if ((uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))) != d[b]) bad++;
    free(d); return bad == 0u;
}
static int plane_is_f(const char *fname)                                                         /* the plane equals the canvas of the file `fname` of the set directory */
{
    char f[600]; snprintf(f, sizeof f, "%s/%s", dir, fname); uint32_t n; uint8_t *d = slurp(f, &n); if (!d) return 0;
    uint32_t bad = 0;
    for (uint32_t b = 0; b < 1728000u; b++) { const uint8_t got = (uint8_t)(mem[0x200000u + b / 2u] >> (8u * (b & 1u))); if (got != d[20480u + b]) bad++; }
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
static uint32_t ph_stall, ph_stall_max, ph_jump_max, ph_axx, ph_axy; static int32_t ph_px = -1, ph_py = -1;
static void ldr_probe(uint32_t dst)      /* a chunk of a picture landing: what the loader bar shows before it (a track at the start, a fill that only grows). An open that shows no progress (a picture change in the viewer) leaves the last bar full under the viewer: its chunks are not judged */
{
    uint32_t fill = 0u; for (uint32_t i = 0; i < 160u; i++) if (fb[178u * 512u + 120u + i] == th_role[TR_ACCENT]) fill++;
    if (dst == 0x40000000u) {
        ldr_skip = fill == 160u; ldr_prev = 0u;
        if (!ldr_skip) { const int track = fb[178u * 512u + 279u] == (uint16_t)((th_role[TR_TEXT_SECONDARY] >> 1) & 0x7BEFu); ldr_opens++; if (!(track && fill == 0u && fb[179u * 512u + 279u] == fb[178u * 512u + 279u] && fb[177u * 512u + 120u] != th_role[TR_ACCENT] && fb[180u * 512u + 120u] != th_role[TR_ACCENT])) ldr_first_ok = 0u; }
    }
    if (ldr_skip) return;
    if (fill < ldr_prev) ldr_mono = 0u;
    if (fill > 0u && (fb[180u * 512u + 120u] == th_role[TR_ACCENT] || fb[177u * 512u + 120u] == th_role[TR_ACCENT] || fb[179u * 512u + 120u] != th_role[TR_ACCENT])) ldr_mono = 0u;      /* the bar is two rows: 178 and 179, no more */
    ldr_prev = fill; if (fill > ldr_max) ldr_max = fill; ldr_chunks++;
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
    const uint16_t ac = th_role[TR_ACCENT], tr = (uint16_t)((th_role[TR_TEXT_SECONDARY] >> 1) & 0x7BEFu);
    const uint8_t ar = (uint8_t)(((ac >> 11) & 31u) << 3), ag = (uint8_t)(((ac >> 5) & 63u) << 2), ab = (uint8_t)((ac & 31u) << 3), tR = (uint8_t)(((tr >> 11) & 31u) << 3), tG = (uint8_t)(((tr >> 5) & 63u) << 2), tB = (uint8_t)((tr & 31u) << 3);
    uint32_t fill[2];
    for (uint32_t r = 0; r < 2u; r++) {
        uint32_t f = 0u, inprefix = 1u;
        for (uint32_t px = 0; px < 160u; px++) {
            const uint32_t byte0 = ((358u + r) * 1200u + 480u) * 2u + px * 3u; uint8_t v[3];
            for (uint32_t c = 0; c < 3u; c++) { const uint32_t bi = byte0 + c; v[c] = (uint8_t)(mem[0x200000u + bi / 2u] >> (8u * (bi & 1u))); }
            const int isf = v[0] == ar && v[1] == ag && v[2] == ab, ist = v[0] == tR && v[1] == tG && v[2] == tB;
            if (isf && inprefix) f++; else { inprefix = 0u; if (!ist) zbp_bad++; }
        }
        fill[r] = f;
    }
    if (fill[0] != fill[1] || fill[0] < zbp_prev) zbp_bad++;
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
static const char *last_text(void) { return texts[(ntext + 63) % 64]; }
static void clear_texts(void) { memset(texts, 0, sizeof texts); ntext = 0; }
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
static void checkpoint(int n)
{
    char want[64]; char path[200];
    switch (n) {
    case 200: chk(200, plane_is_rgb("v00.rgb") && tg.starts == 42u && tg.loads == 1u && !said("ERROR") && !said("DAMAGED")); clear_texts(); break;                          /* a version 1 picture opened from the grid: the plane is its decode, 42 tiles, one table load */
    case 201: chk(201, plane_is_rgb("v01.rgb") && tg.starts == 84u && tg.loads == 2u); break;                                                                                 /* the next picture (a 500 x 300 one-level file): another table load for another file at the same base */
    case 202: chk(202, plane_is_f("v02.tgy") && tg.starts == 84u); break;                                                                                                     /* a version 0 picture in the same library still lands as before (no decoder start) */
    case 203: chk(203, said("DECODER ERROR") && vid_bus == 0u); clear_texts(); break;                                                                                         /* a damaged stream: the viewer is left and the status line says so */
    case 204: chk(204, said("PICTURE DAMAGED")); clear_texts(); break;                                                                                                        /* a damaged tile index */
    case 205: chk(205, said("NOT A TGY PICTURE")); clear_texts(); break;                                                                                                      /* a damaged header */
    case 207: force_ovf = 1; v1_starts_before = tg.starts; clear_texts(); break;                                                                                              /* the landing path counts an overflow: the next open must not show a damaged picture */
    case 208: chk(208, said("PICTURE DAMAGED IN TRANSFER") && vid_bus == 0u && tg.starts == v1_starts_before); force_ovf = 0; clear_texts(); break;                           /* a version 1 picture whose landing overflowed is refused before the decoder starts */
    case 210: chk(210, plane_is_rgb("v00.rgb") && zn == 1u && zs[0].layout == 2u && zs[0].w == 1000u && zs[0].h == 900u && zk < 0); break;                                         /* the v1 zoom scenario: v00 is open (fit), its full level is the one stop */
    case 211: chk(211, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && info.copy_ms != GI_NONE && info.land_ms != GI_NONE && z_sx >= 96 && z_sx <= 104 && z_sy >= 86 && z_sy <= 94); break;                                                                          /* A: the 1:1 view, decoded tile by tile into the window, equals the reference decode of the full level */
    case 212: chk(212, zk == 0 && z_shown && z_sx > 0 && z_sy > 0 && v1_view_matches("v00_full.rgb", 1000u, 900u)); break;                                                  /* panned right and down: still the reference pixels */
    case 213: chk(213, zk < 0 && vbase == GAL_PLANE_BASE && plane_is_rgb("v00.rgb")); break;                                                                                /* A: back to fit, the fit plane untouched */
    case 214: chk(214, zk == 0 && z_shown && v1_view_matches("v00_full.rgb", 1000u, 900u) && v1z_idx_ok); break;                                                            /* zoom again: the same, the index kept */
    case 215: chk(215, zn == 0u && zk < 0 && vid_bus == 1u); break;                                                                                                         /* the rotated picture (EXIF 180 degrees): no zoom stop */
    case 216: chk(216, zk < 0 && !z_shown && vbase == GAL_PLANE_BASE); break;                                                                                               /* A does nothing there */
    case 217: force_ovf = 1; clear_texts(); break;                                                                                                                         /* the landing path counts an overflow: the first tile cannot be read */
    case 218: chk(218, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); force_ovf = 0; clear_texts(); break;                                                       /* the zoom is left, the grid is back with the message */
    case 219: v1z_cap = 600u; break;                                                                                                                                         /* an index of 768 bytes (64 tiles) no longer fits the buffer */
    case 220: chk(220, zn == 0u && zk < 0 && vid_bus == 1u); v1z_cap = V1Z_IDX_MAX; break;                                                                                  /* so that picture has no zoom stop */
    case 221: chk(221, zn == 1u && !v1z_idx_ok && zk < 0); fail_pic_reads = 3; pass_pic_reads = 0; clear_texts(); break;                                                      /* opened again with the normal buffer; the next card reads fail: the zoom's index cannot be read */
    case 222: chk(222, said("ZOOM INDEX READ FAILED") && zk < 0 && !v1z_idx_ok && vid_bus == 1u); fail_pic_reads = 0; clear_texts(); break;                                  /* the fit view stays, with the message */
    case 223: chk(223, zk == 0 && z_shown && v1z_idx_ok && v1_view_matches("v00_full.rgb", 1000u, 900u)); break;                                                            /* the card reads work again: the zoom opens */
    case 224: chk(224, zn == 0u && zk < 0 && vid_bus == 1u); break;                                                                                                         /* a 500 x 300 picture: its only level is no larger than the screen */
    case 226: v1z_smax = 100u; break;                                                                                                                                         /* no tile stream is allowed to be longer than 100 bytes */
    case 227: chk(227, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); v1z_smax = V1Z_STREAM_MAX; clear_texts(); break;                                             /* the first tile is refused: the zoom is left with the message */
    case 228: force_short = 1; clear_texts(); break;                                                                                                                         /* a landing of one word too few (counted by the landing path) */
    case 229: chk(229, said("PICTURE READ FAILED") && vid_bus == 0u && zk < 0); force_short = 0; clear_texts(); break;                                                     /* is not accepted either */
    case 225: chk(225, land_unaligned == 0u); break;                                                                                                                         /* no landing read of a picture was unaligned (the tile streams read as 4 KB aligned supersets) */
    case 206: chk(206, said("NEEDS THE HW DECODER") && tg.starts == 0u); clear_texts(); break;                                                                                /* a core without the decoder */
    case 300: chk(300, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "1/18")); break;                                                                               /* the details scenario: the grid, picture 1 selected */
    case 309: chk(309, vid_bus == 1u && plane_is(3) && sel == 3u && !said("DETAILS")); break;                                                                  /* Select + Right together is the next picture, not the page */
    case 301: chk(301, said("DETAILS") && said("NAME") && said("Picture 00") && said("1 OF 18") && said("SIZE") && said("FILE") && said("NOT READ YET") && said("NOT COMPUTED YET") && said("LUMA") && said("RED") && said("GREEN") && said("BLUE") && said("B OR SELECT BACK") && !thumbs_on_screen(0, 16u)); clear_texts(); break;      /* Select on the grid: the page of the selected picture */
    case 302: chk(302, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "1/18") && !said("DETAILS")); clear_texts(); break;                                                                  /* B: the grid again, nothing changed */
    case 303: chk(303, said("DETAILS") && said("Picture 01") && said("2 OF 18") && !said("Picture 00")); clear_texts(); break;                                               /* Right, Select: the next picture's page */
    case 304: chk(304, thumbs_on_screen(0, 16u) && !strcmp(last_text(), "2/18") && sel == 1u); break;                                                                          /* Select closes it too */
    case 305: chk(305, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is(1) && land_ovf == 0u); break;                                                                      /* A: the picture view */
    case 306: chk(306, vid_bus == 0u && said("DETAILS") && said("Picture 01") && said("2 OF 18")); clear_texts(); break;                                                       /* Select in the picture view: the page replaces it (the viewer is off) */
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
    case 23: chk(32, said("4D50331B") && count_text("NONE YET") == 4 && said(" MS") && said("432128 WORDS, 0 LOST")); chk(33, said("NONE") && !said("FAILED")); chk(44, said("L1 LOADED 1 SAVED 1")); chk(125, ldr_opens > 0u && ldr_chunks > 0u && ldr_first_ok && ldr_mono && ldr_max >= 100u && ldr_max < 160u); clear_texts(); break;   /* Info after viewing pictures: the last landing */
    case 24: chk(34, said("OPEN FAILED") && said("0 WORDS, 0 LOST")); clear_texts(); break;                                                                          /* Info after a failed open: the error, and no landing counts carried over */
    case 25: chk(35, said("RESUME LAST PICTURE") && said("ON") && !said("OFF")); clear_texts(); break;                                                                    /* Settings page, resume on (the default) */
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
    case 61: chk(61, plane_is_f("b299.tgy") && sel == count - 1u); break;                                                                                                 /* A on the last picture of 300: the file of index 299 */
    case 62: chk(62, plane_is_f("b298.tgy") && sel == count - 2u && page == (count - 2u) / 16u); break;                                                                 /* Left in the viewer */
    case 63: chk(63, plane_is_f("b000.tgy") && sel == 0u && page == 0u); break;                                                                                         /* Right, Right: past the end to the first picture, across 18 pages */
    case 69: chk(69, sel == 0u && big_grid_ok()); break;
    case 70: chk(70, sel == 298u && big_grid_ok()); break;
    case 71: chk(71, sel == 299u && big_grid_ok()); break;
    case 64: chk(64, plane_is_f("b150.tgy") && sel == 150u && page == 9u); break;                                                                                       /* a picture in the middle, opened from its page */
    case 72: fail_pic_reads = 1; pass_pic_reads = 0; clear_texts(); break;                                                                                              /* the header read fails once */
    case 73: chk(72, vid_bus == 1u && fail_pic_reads == 0 && plane_is(count - 1u) && !said("FAILED")); clear_texts(); break;                                                          /* the header is read again and the picture shows */
    case 75: fail_pic_reads = 1; pass_pic_reads = 1; clear_texts(); break;                                                                                               /* the header reads, the first landing chunk fails once */
    case 76: chk(74, vid_bus == 1u && fail_pic_reads == 0 && plane_is(count - 1u) && land_ovf == 0u && land_words == 1728512u / 4u && !said("FAILED")); clear_texts(); break;           /* the landing is repeated whole: the picture shows and the word count is exact, not doubled */
    case 77: fail_pic_reads = 2; pass_pic_reads = 1; clear_texts(); break;                                                                                               /* the landing fails twice in a row */
    case 74: chk(73, said("PICTURE READ FAILED") && vid_bus == 0u && fail_pic_reads == 0); clear_texts(); break;                                                                /* two failed reads: reported, the viewer stays off */
    case 81: chk(81, vid_bus == 1u && vbase == GAL_PLANE_BASE && plane_is_f("z00.tgy")); break;                                                                          /* A on picture 1: the fit canvas, as before */
    case 82: clk_step = 1000000u; zoom_max_len = 0u; zoom_max_rs = 0u; zoom_phase = 1; break;                                                                                 /* from here a clock read is a frame */
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
    case 109: clk_step = 50u; break;                                                                                                                                         /* almost no time passes between key polls */
    case 110: { int32_t x = -1, y = -1; chk(104, view_matches("z00.tgy", &x, &y) && x == 0 && y == 0); } break;                                                              /* 20 polls with Right held and no frame elapsed: the view has not moved (a step is a frame, not a pass) */
    case 111: clk_step = 1000000u; break;
    case 112: chk(105, fit_or_exact("z04.tgy")); break;                                                                                                                      /* Down held from the first instant, loading under way: fit plane or an exact zoom view, never unloaded rows */
    case 103: { int32_t x = -1, y = -1; chk(102, sel == 5u && tile_view_matches("z05.tgy", &x, &y) && x == 748 && y == 390); chk(106, blit_visible == 0u && blit_bad == 0u && tile_reads >= 40u && zoom_max_len <= 49152u && zoom_max_rs <= 1u && blit_max_pass > 0u && blit_max_pass <= 68u);      /* and at most 64 Talos commands between two key polls */
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
    case 133: chk(134, plane_is_f("z06.tgy")); chk(129, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z06.tgy") && zbp_n >= 1u && zbp_bad == 0u && zbp_max >= 1u); break;      /* the zoom's loader bar was on the fit plane while the first view loaded, and the plane is the picture again */
    case 136: chk(130, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z06.tgy")); break;
    case 134: chk(131, sel == 5u && zk == -1); break;
    case 135: chk(132, zk == -1 && vbase == GAL_PLANE_BASE && plane_is_f("z05.tgy") && zbp_max >= 1u); break;
    case 140: { int32_t x = -1, y = -1; chk(137, tile_view_matches("z06.tgy", &x, &y) && zk == 1); map_x0 = x; map_y0 = y; } break;
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
    case 119: { int32_t x = -1, y = -1; chk(113, tile_view_matches("z05.tgy", &x, &y) && y == 780 && blit_visible == 0u && ph_jump_max <= 16u && ph_stall_max <= 300u); } break;                                          /* down to the bottom edge */
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
    setw[0] = 1u << 27; setw[1] = 0x40000001u; setw[2] = 0x4000005Eu; setw[5] = 0x40000000u; setw[6] = 0x40000000u;      /* the package's defaults of the inherited variables: layout 1, appearance, word 2 with bit 0 clear (resume on), the player's marks in words 5 and 6 */
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
        S4[k++] = KEY_A; S4[k++] = 0; S4[k++] = CP(93);
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
        nscript = k; memcpy(script, S4, (size_t)k * sizeof S4[0]);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "details")) {                                                       /* the picture details page: from the grid and from the picture view */
        const uint32_t SD[] = { CP(300), KEY_SELECT, 0, CP(301), KEY_B, 0, CP(302), KEY_RIGHT, 0, KEY_SELECT, 0, CP(303), KEY_SELECT, 0, CP(304), KEY_A, 0, CP(305), KEY_SELECT, 0, CP(306), KEY_B, 0, CP(307), KEY_RIGHT, 0, CP(308), KEY_SELECT | KEY_RIGHT, 0, CP(309), END };
        nscript = (int)(sizeof SD / sizeof SD[0]); memcpy(script, SD, sizeof SD);
        if (!setjmp(done)) gallery_app_main();
        printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails); return fails ? 1 : 0;
    }
    if (argc > 2 && !strcmp(argv[2], "v1zoom")) {                                                          /* zoom of a version 1 picture: its full level decoded by the hardware into the window */
        uint32_t S6[2048]; int k = 0;
#define H6(key, n) do { for (int _i = 0; _i < (n); _i++) S6[k++] = (key); } while (0)
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(210); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(211);
        H6(KEY_RIGHT, 40); H6(KEY_DOWN, 30); H6(0, 300); S6[k++] = CP(212);
        S6[k++] = KEY_A; S6[k++] = 0; H6(0, 3); S6[k++] = CP(213); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(214);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = CP(219);                                          /* back to fit, then to the grid; the index buffer too small */
        S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(220); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 20);                                                    /* open v00: no zoom stop; A does nothing */
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(221);                                                    /* open v00 again: a stop; then the index read fails */
        S6[k++] = KEY_A; S6[k++] = 0; H6(0, 20); S6[k++] = CP(222); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 200); S6[k++] = CP(223);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); S6[k++] = KEY_RIGHT; S6[k++] = 0; S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(224);
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); for (int i = 0; i < 5; i++) { S6[k++] = KEY_RIGHT; S6[k++] = 0; } S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(215); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 20); S6[k++] = CP(216);      /* the rotated picture is v06 */
        S6[k++] = KEY_B; S6[k++] = 0; H6(0, 3); for (int i = 0; i < 6; i++) { S6[k++] = KEY_LEFT; S6[k++] = 0; } S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(217); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(218); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(226); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(227); S6[k++] = KEY_A; S6[k++] = 0; S6[k++] = CP(228); S6[k++] = KEY_A; S6[k++] = 0; H6(0, 60); S6[k++] = CP(229); S6[k++] = CP(225); S6[k++] = END;
        nscript = k; memcpy(script, S6, (size_t)k * sizeof S6[0]);
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
            S3[k++] = KEY_R1; S3[k++] = 0; S3[k++] = CP(60); S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(60); S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(60);      /* the end: nothing moves */
            S3[k++] = KEY_A; S3[k++] = 0; S3[k++] = CP(61); S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = CP(62);
            S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = KEY_RIGHT; S3[k++] = 0; S3[k++] = CP(63); S3[k++] = KEY_B; S3[k++] = 0; S3[k++] = CP(60);
            for (uint32_t p = 0; p < 10u; p++) { S3[k++] = KEY_R1; S3[k++] = 0; }                                         /* 0 -> 160 by pages */
            S3[k++] = CP(60); S3[k++] = KEY_UP; S3[k++] = 0; S3[k++] = KEY_UP; S3[k++] = 0; S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = KEY_LEFT; S3[k++] = 0; S3[k++] = CP(60);      /* 160 - 4 - 4 - 1 - 1 = 150, on page 9 */
            S3[k++] = KEY_A; S3[k++] = 0; S3[k++] = CP(64); S3[k++] = KEY_B; S3[k++] = 0; S3[k++] = CP(60);
            for (uint32_t p = 0; p < 9u; p++) { S3[k++] = KEY_R1; S3[k++] = 0; }                                          /* 150 -> 294 */
            S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(70); S3[k++] = KEY_DOWN; S3[k++] = 0; S3[k++] = CP(71); S3[k++] = END;      /* a row down, then Down from the last row goes to the last picture */
        }
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
