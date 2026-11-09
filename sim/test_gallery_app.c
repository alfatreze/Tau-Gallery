/* Host test of the Tau Gallery M1 firmware (fw2/gallery/gallery_app.c plus gallery_fmt.c and gallery_hw.c) against a model of the hardware that serves the REAL files the Python converter wrote:
 * the data slots read from the files, the mailbox writes a framebuffer, the landing path writes the SDRAM plane, the viewer mode sequencer, the keys come from a script. Checkpoints in the script compare the
 * framebuffer thumbnails with the atlas pixel for pixel, the plane with the picture's canvas byte for byte, the viewer state and the text of the status line.
 *   test_gallery_app DIR     (DIR holds tau-library.tdb and p00.tgy .. pNN.tgy, written by sim/test_gallery_app.py)
 * Prints "GALLERY APP ok n" / "GALLERY APP FAIL n"; exit 1 on a failure. Mutants are made by the Python driver (it edits copies of the firmware sources). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#define P2_HOST 1
#define HW_N (8u * 1024u * 1024u)
static uint16_t mem[HW_N];                         /* SDRAM, halfwords: the plane at 0x200000 */
static uint16_t fb[512u * 1024u];                  /* the UI framebuffer, halfwords, stride 512 */
static uint32_t clk, irq_pend, vid_req, vid_bus, vid_sw, vid_due, vbase, vstride, land_base, land_words, land_ovf, input_n;
static int force_ovf, force_short, viewer_dead;
static void tick(void) { if (!viewer_dead && vid_req != vid_bus && vid_due && clk >= vid_due) { vid_bus = vid_req; vid_sw++; vid_due = 0; } }
static uint32_t next_input(void);
static uint32_t rd(uint32_t a)
{
    switch (a) {
    case 0x80000084u: return 0;
    case 0x800001ECu: return (vid_sw << 16) | (vid_req << 2) | vid_bus;
    case 0x800001F0u: return vbase;
    case 0x800001F4u: return vstride;
    case 0x80000208u: return 0x80000001u;
    case 0x8000020Cu: return land_words;
    case 0x80000210u: return force_ovf ? 1u : land_ovf;
    case 0x8000003Cu: return next_input();
    default: return 0;
    }
}
static uint32_t cyc_rd(void) { clk += 50u; tick(); return clk; }
static void wr(uint32_t a, uint32_t v)
{
    switch (a) {
    case 0x800001ECu: vid_req = v & 1u; vid_due = clk + 2u * 1111111u; break;
    case 0x800001F0u: vbase = v & 0x1FFFFFFu; break;
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
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)to; reads++;
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
void irq_disable(void) { }
int platform_version_ok(void) { return 1; }
void platform_halt_pattern(void) { exit(3); }
void platform_probe(void) { }

/* ---- the key script ---- */
#define CP(n) (0x10000u | (n))
#define END 0x20000u
static uint32_t script[256]; static int nscript;
static jmp_buf done;
static int fails;
static void chk(int n, int ok) { printf("GALLERY APP %s %d\n", ok ? "ok" : "FAIL", n); if (!ok) fails++; }
static void checkpoint(int n);
static uint32_t next_input(void)
{
    for (;;) {
        if (input_n >= (uint32_t)nscript) longjmp(done, 1);
        const uint32_t t = script[input_n++];
        if (t == END) longjmp(done, 1);
        if (t & 0x10000u) { checkpoint((int)(t & 0xFFFFu)); continue; }
        return t;
    }
}
#define main gallery_app_main
#include "../fw2/gallery/gallery_fmt.c"
#include "../fw2/gallery/gallery_pattern.c"
#include "../fw2/gallery/gallery_hw.c"
#include "../fw2/gallery/gallery_app.c"
#undef main

static uint32_t count;
static const uint8_t *thumb_of(uint32_t i) { uint32_t atlas = (uint32_t)idx_data[20] | (uint32_t)idx_data[21] << 8 | (uint32_t)idx_data[22] << 16; return idx_data + atlas + i * 13824u; }
static int thumbs_on_screen(uint32_t first, uint32_t n)                                          /* every pixel of the page's thumbnails against the atlas */
{
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *t = thumb_of(first + k); const uint32_t x = 2u + (k % 4u) * 100u, y = 22u + (k / 4u) * 76u;
        for (uint32_t r = 0; r < 72u; r++) for (uint32_t c = 0; c < 96u; c++) { const uint32_t o = (r * 96u + c) * 2u; if (fb[(y + r) * 512u + x + c] != (uint16_t)(t[o] | t[o + 1] << 8)) return 0; }
        for (uint32_t c = 0; c < 96u; c++) { const uint16_t v = fb[(y + 72u) * 512u + x + c]; if (v != 0u && v != RGB565(255, 170, 40)) return 0; }          /* nothing written under the thumbnail: black or the selection frame */
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
static int said(const char *s) { for (int i = 0; i < 64; i++) if (strstr(texts[i], s)) return 1; return 0; }
static const char *last_text(void) { return texts[(ntext + 63) % 64]; }
static void clear_texts(void) { memset(texts, 0, sizeof texts); ntext = 0; }
static void checkpoint(int n)
{
    char want[64]; char path[200];
    switch (n) {
    case 1: chk(1, thumbs_on_screen(0, count < 16u ? count : 16u)); snprintf(want, sizeof want, "1/%u", count); chk(2, said(want)); chk(3, said("TAU GALLERY") && !said("FAILED")); break;
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
    case 16: chk(24, said("CONTROLS") && said("HELP") && said("A OPEN") && !thumbs_on_screen(0, count < 16u ? count : 16u)); clear_texts(); break;           /* Start: the menu replaces the grid */
    case 17: chk(25, said("OPEN THE PICTURE") && said("1/14") && !said("HELP")); clear_texts(); break;                                                           /* A on Controls: the list, its first row of 14 */
    case 18: chk(26, said("CONTROLS") && said("HELP") && !said("OPEN THE PICTURE")); clear_texts(); break;                                                       /* B: back in the menu */
    case 19: chk(27, said("ADD PHOTOS") && said("1/3") && !said("MENU")); clear_texts(); break;                                                                  /* Down, A: Help page 1 of 3 */
    case 20: chk(28, said("WHERE THINGS ARE") && said("2/3") && !said("ADD PHOTOS")); clear_texts(); break;                                                      /* Down turns the page */
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
    const uint32_t S[] = { CP(1), KEY_START, 0, CP(16), KEY_A, 0, CP(17), KEY_B, 0, CP(18), KEY_DOWN, 0, KEY_A, 0, CP(19), KEY_DOWN, 0, CP(20), KEY_B, 0, KEY_B, 0, CP(21), KEY_RIGHT, 0, KEY_RIGHT, 0, CP(2), KEY_A, 0, CP(3), KEY_RIGHT, 0, CP(4), KEY_LEFT, 0, KEY_LEFT, 0, CP(5), KEY_LEFT, 0, KEY_LEFT, 0, CP(6), KEY_B, 0, CP(7),
                          KEY_L1, 0, CP(8), KEY_R1, 0, CP(9), KEY_L1, 0, KEY_RIGHT, 0, KEY_R1, 0, CP(13), KEY_L1, 0, CP(10), KEY_A, 0, CP(11), KEY_RIGHT, 0, KEY_A, 0, CP(12),
                          KEY_RIGHT, 0, KEY_A, 0, CP(14), KEY_A, 0, CP(15), END };
    nscript = (int)(sizeof S / sizeof S[0]); memcpy(script, S, sizeof S);
    if (!setjmp(done)) gallery_app_main();
    printf(fails ? "GALLERY APP FAILED %d\n" : "GALLERY APP PASSED (0 failures)\n", fails);
    return fails ? 1 : 0;
}
