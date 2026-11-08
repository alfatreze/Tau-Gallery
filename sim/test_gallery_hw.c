/* Host test of fw2/gallery/gallery_hw.c against a model of the Tau Gallery hardware: the SDRAM mailbox and plane, the mode sequencer, the landing path, the interrupt block and the data-slot reads.
 * The model is what docs/M0_DESIGN.md says the RTL does (tests of the RTL itself: make test-rtl-view-soc, -land, -irq-soc). What this checks is the firmware: that the plane lands in the frozen format,
 * that the switch, landing and interrupt tests measure and judge what they claim to, and (with a broken model: -DMUT=n) that a wrong landing path, a missing interrupt or a failed read is NOT reported as good.
 * Prints "GALLERY HW ok n" per check; exit 1 on any failure. Mutants: 1 landing swaps bytes, 2 landing writes nothing, 3 landing drops the last word, 4 interrupts never fire, 5 mode switch never completes, 6 reads fail, 7 landed blocks after the first repeat block 0, 8 the plane is written at 5/6/5 bits. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define P2_HOST 1
#ifndef MUT
#define MUT 0
#endif
#define HW_N (16u * 1024u * 1024u)
static uint16_t mem[HW_N];                       /* SDRAM, halfwords */
static uint32_t clk, next_vs = 1111111u, irq_mask, irq_pend, vid_req, vid_bus, vid_sw, vid_due, vbase, vstride, land_base, land_words, land_ovf, sdr_a, sdr_d, sdr_rd, seq;
extern void irq_handler(uint32_t pend);
static void tick(void)
{
    while (clk >= next_vs) { next_vs += 1111111u; irq_pend |= 2u; if ((irq_mask & irq_pend & 2u) && MUT != 4) irq_handler(irq_pend & irq_mask); }
    if (vid_req != vid_bus && vid_due && clk >= vid_due && MUT != 5) { vid_bus = vid_req; vid_sw++; vid_due = 0; irq_pend |= 8u; if ((irq_mask & 8u) && MUT != 4) irq_handler(irq_pend & irq_mask); }
}
static uint32_t rd(uint32_t a)
{
    switch (a) {
    case 0x80000000u + 0x1Cu: return 0;
    case 0x80000084u: return 0;                                   /* R_SDR_STATUS: never busy */
    case 0x80000080u: return sdr_rd;                              /* R_SDR_RDATA_A */
    case 0x800001ECu: return (vid_sw << 16) | (vid_req << 2) | vid_bus;
    case 0x800001F0u: return vbase;
    case 0x800001F4u: return vstride;
    case 0x80000208u: return 0x80000001u;                         /* LAND_STATUS: present, idle */
    case 0x8000020Cu: return land_words;
    case 0x80000210u: return land_ovf;
    case 0x80000214u: return irq_pend;
    case 0x80000030u: return seq << 8;                            /* R_TGT_GO */
    default: if (a == 0x80000000u + 0x0u) return 0; return 0;
    }
}
static uint32_t cyc_rd(void) { clk += 50u; tick(); return clk; }
static void wr(uint32_t a, uint32_t v)
{
    switch (a) {
    case 0x800001ECu: vid_req = v & 1u; vid_due = clk + 2u * 1111111u; break;       /* the sequencer: a request lands two frames later */
    case 0x800001F0u: vbase = v & 0x1FFFFFFu; break;
    case 0x800001F4u: vstride = v & 0xFFFFu; break;
    case 0x80000200u: land_base = v & 0x1FFFFFFu; break;
    case 0x80000204u: if (v & 1u) { land_words = 0; land_ovf = 0; } break;
    case 0x80000214u: irq_pend &= ~v; break;
    default: break;
    }
}
/* The register macros: R_CYCLES is read through cyc_rd(), everything else through rd() (R_CYCLES is defined by mmio.h, which expands the macro in cycles()). */
#define REG_RD(a) ((a) == 0x8000000Cu ? cyc_rd() : rd(a))
#define REG_WR(a, v) wr((a), (v))
#include "../fw2/platform/mmio.h"
#include "../fw2/gfx/gfx.h"
#include "../fw2/stream/slot.h"
/* the two mailbox calls, the data-slot read and the interrupt controls, modelled */
int gfx_mb_write(uint32_t a, uint32_t d) { if (a + 1u >= HW_N) return -1; if (MUT == 8 && a >= 0x200000u && a < 0x300000u) d &= 0xF8FCF8FFu; mem[a] = (uint16_t)d; mem[a + 1u] = (uint16_t)(d >> 16); return 0; }
int gfx_mb_read(uint32_t a, uint32_t *o) { if (a + 1u >= HW_N) return -1; *o = (uint32_t)mem[a] | (uint32_t)mem[a + 1u] << 16; return 0; }
uint32_t _tag_start[1024];
static uint8_t fbyte(uint32_t i) { return (uint8_t)(i * 7u + (i >> 8) * 13u + 5u); }       /* the file in slot 6 */
static uint32_t reads, read_fail_after = 0xFFFFFFFFu;
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)to; reads++;
    if (slot != 6u || MUT == 6 || reads > read_fail_after) { seq++; return 0; }
    if ((dst >> 28) == 4u) {                                                     /* the landing window: bytes land in file order (the RTL byte-reverses the big-endian bridge words) */
        const uint32_t words = len / 4u - (MUT == 3 ? 1u : 0u);
        if (MUT != 2) for (uint32_t i = 0; i < words * 4u; i++) {
            const uint32_t pos = ((dst & 0x0FFFFFFFu) + i), h = land_base + pos / 2u, b = pos & 1u;
            const uint8_t v = fbyte(off + (MUT == 7 && pos >= 4096u ? (i & 4095u) : i));      /* MUT 7: every block after the first holds block 0 */
            if (MUT == 1) { const uint32_t sw = pos ^ 1u; mem[land_base + sw / 2u] = (uint16_t)((mem[land_base + sw / 2u] & ~(0xFFu << (8u * (sw & 1u)))) | (uint32_t)v << (8u * (sw & 1u))); (void)h; (void)b; }
            else mem[h] = (uint16_t)((mem[h] & ~(0xFFu << (8u * b))) | (uint32_t)v << (8u * b));
        }
        land_words += words;
    } else for (uint32_t i = 0; i < len; i++) ((uint8_t *)_tag_start)[i] = fbyte(off + i);
    clk += len * 78u; seq++; irq_pend |= 1u;                                    /* about 850 KB/s */
    if ((irq_mask & 1u) && MUT != 4) irq_handler(irq_pend & irq_mask);
    return 1;
}
void irq_enable(uint32_t m) { irq_mask = m; }
void irq_disable(void) { irq_mask = 0; }
#include "../fw2/gallery/gallery_pattern.c"
#include "../fw2/gallery/gallery_hw.c"
static int fails;
static void chk(int n, int ok) { printf("GALLERY HW %s %d\n", ok ? "ok" : "FAIL", n); if (!ok) fails++; }
static uint32_t prog_calls;
static void prog(uint32_t y) { (void)y; prog_calls++; }
int main(void)
{
    /* 1: the plane, every pixel of every pattern, in the frozen format (byte b of the plane is the low or high byte of halfword b/2) */
    for (uint32_t pat = 0; pat < GP_COUNT; pat++) {
        memset(mem, 0xEE, sizeof mem);
        const uint32_t bad = gal_fill(pat, prog);
        uint32_t wrong = 0;
        for (uint32_t y = 0; y < GAL_H; y++) for (uint32_t x = 0; x < GAL_W; x++) {
            uint8_t px[3]; gal_pixel(pat, x, y, px);
            for (uint32_t c = 0; c < 3; c++) { const uint32_t b = y * 2400u + 3u * x + c; const uint8_t got = (uint8_t)(mem[GAL_PLANE_BASE + b / 2u] >> (8u * (b & 1u))); if (got != px[c]) wrong++; }
        }
        chk(1 + (int)pat, MUT == 8 ? (wrong > 0u) : (bad == 0u && wrong == 0u));          /* MUT 8: the 5/6/5 writes must show as wrong pixels */
    }
    chk(5, prog_calls == 4u * 24u);                                        /* every 30 lines of 720 */
    chk(6, mem[GAL_PLANE_BASE + 864000u] == 0xEEEEu && mem[GAL_PLANE_BASE - 1u] == 0xEEEEu);   /* nothing outside the plane */
    /* 2: the viewer on and off */
    const uint32_t don = gal_view_on();
    chk(7, MUT == 5 ? don == 0u : (don > 0u && vid_bus == 1u && vbase == GAL_PLANE_BASE && vstride == 1200u));
    const uint32_t doff = gal_view_off();
    chk(8, MUT == 5 ? 1 : (doff > 0u && vid_bus == 0u && doff >= 2u * 1111111u));
    /* 3: the switch test */
    gal_sw_t sw; gal_switch_test(&sw, 3u, 20u);
    chk(9, MUT == 5 ? (sw.done == 0u && sw.ok < sw.n) : (sw.n == 6u && sw.ok == 6u && sw.done == 6u && sw.worst_cyc >= 2u * 1111111u && sw.best_cyc <= sw.worst_cyc));
    /* 4: landing reads */
    uint32_t bad_all = 0, ok_all = 1;
    for (uint32_t k = 0; k < GAL_LAND_KINDS; k++) {
        gal_land_t r; gal_land_test(&r, gal_land_len[k]);
        const uint32_t want_checked = k ? 2048u : 1024u;
        const int good = r.started && r.cmd_ok && r.idle_ok && r.ovf == 0u && r.words == r.len / 4u && r.checked == want_checked && r.bad == 0u;
        if (!good) ok_all = 0;
        bad_all += r.bad;
        if (MUT == 0) { chk(10 + (int)k, good && gal_kbps(r.len, r.cmd_cyc) > 700u && gal_kbps(r.len, r.cmd_cyc) < 900u); }
    }
    chk(14, (MUT == 1 || MUT == 2 || MUT == 3 || MUT == 6 || MUT == 7) ? !ok_all : ok_all);   /* a broken landing path or a failed read is caught by a field the page judges on; the others leave it alone */
    (void)bad_all;
    /* 4b: where landed data goes (the map) and the plane read back */
    gal_map_t mp; gal_land_map(&mp);
    if (MUT == 0) chk(16, mp.cmd_ok && mp.idle_ok && mp.words == 4096u && mp.ovf == 0u && mp.bad_block == 0u && mp.match[0] == 0 && mp.match[1] == 1 && mp.match[2] == 2 && mp.match[3] == 3);
    else if (MUT == 7) chk(16, mp.match[0] == 0 && mp.match[1] == 0 && mp.match[2] == 0 && mp.match[3] == 0 && mp.bad_block == 2u && mp.bad_word == 0u);        /* duplicated block shows as its source, first bad is block 1 */
    else if (MUT == 2) chk(16, mp.match[0] == -2 && mp.match[3] == -2 && mp.bad_block == 1u && mp.got == 0xA5A5A5A5u);                                         /* never written: poison */
    else if (MUT == 3) chk(16, mp.match[3] == -1 && mp.bad_block == 4u && mp.bad_word == 1023u && mp.got == 0xA5A5A5A5u);                                       /* the last word is missing */
    else if (MUT == 1) chk(16, mp.match[0] == -1 && mp.bad_block == 1u);
    else if (MUT == 6) chk(16, !mp.cmd_ok);
    else chk(16, mp.cmd_ok && mp.bad_block == 0u);
    { gal_fill(GP_RAMP, 0); gal_verify_t vr; gal_plane_verify(&vr, GP_RAMP);
      if (MUT == 8) chk(17, vr.bad > 0u && vr.words == 432000u); else chk(17, vr.bad == 0u && vr.words == 432000u && vr.timeouts == 0u); }
    /* 5: interrupts */
    read_fail_after = 0xFFFFFFFFu; reads = 0;
    gal_irq_t ir; gal_irq_test(&ir);
    if (MUT == 0) chk(15, ir.vsync >= 59u && ir.vsync <= 61u && ir.cmd == 16u && ir.cmd_n == 16u && ir.sw == 2u && ir.sw_n == 2u);
    else chk(15, (MUT == 4 || MUT == 5) ? (ir.cmd != ir.cmd_n || ir.sw != ir.sw_n) : 1);   /* a missing interrupt shows in the counts the page compares */
    printf(fails ? "GALLERY HW FAILED %d\n" : "GALLERY HW PASSED (0 failures)\n", fails);
    return fails ? 1 : 0;
}
