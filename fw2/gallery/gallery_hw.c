#pragma GCC optimize ("Os")
#include "gallery_hw.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../stream/slot.h"

const uint32_t gal_land_len[GAL_LAND_KINDS] = { 4096u, 16384u, 65536u, 131072u };
                                            /* the 4 KB landing zone in core RAM (fw2/platform/link.ld) */
extern void irq_enable(uint32_t mask), irq_disable(void);              /* fw2/platform/start.S */

static void spin_ms(uint32_t ms) { const uint32_t t = cycles(), n = (CLK_HZ / 1000u) * ms; while ((uint32_t)(cycles() - t) < n) { } }

uint32_t gal_kbps(uint32_t bytes, uint32_t cyc) { return cyc ? (uint32_t)(((uint64_t)bytes * CLK_HZ) / ((uint64_t)cyc * 1000u)) : 0u; }

uint32_t gal_fill(uint32_t pat, void (*progress)(uint32_t y))
{
    static uint8_t line[GAL_LINE_BYTES];
    uint32_t bad = 0;
    for (uint32_t y = 0; y < GAL_H && bad < 8u; y++) {
        gal_line(pat, y, line);
        const uint32_t a = GAL_PLANE_BASE + y * GAL_LINE_WORDS16;
        for (uint32_t k = 0; k < GAL_LINE_WORDS32; k++) if (gfx_mb_write(a + 2u * k, gal_pack32(line, k))) { bad++; if (bad >= 8u) break; }
        if (progress && y % 30u == 0u) progress(y);
    }
    return bad;
}

uint32_t gal_vid_switches(void) { return REG_RD(R_VID_MODE) >> 16; }
static uint32_t go(uint32_t want)
{
    const uint32_t t0 = cycles();
    REG_WR(R_VID_MODE, want);
    while ((REG_RD(R_VID_MODE) & 1u) != want) if ((uint32_t)(cycles() - t0) > CLK_HZ / 2u) return 0u;
    const uint32_t d = cycles() - t0;
    return d ? d : 1u;
}
uint32_t gal_view_on(void) { REG_WR(R_VIEW_STRIDE, GAL_LINE_WORDS16); REG_WR(R_VIEW_BASE, GAL_PLANE_BASE); return go(1u); }
uint32_t gal_view_off(void) { return go(0u); }

void gal_switch_test(gal_sw_t *r, uint32_t round_trips, uint32_t dwell_ms)
{
    r->n = r->ok = r->done = r->worst_cyc = 0; r->best_cyc = 0xFFFFFFFFu;
    const uint32_t s0 = gal_vid_switches();
    for (uint32_t i = 0; i < round_trips; i++)
        for (uint32_t s = 0; s < 2u; s++) {                                              /* viewer, then UI */
            const uint32_t d = s ? gal_view_off() : gal_view_on();
            r->n++;
            if (d) { r->ok++; if (d > r->worst_cyc) r->worst_cyc = d; if (d < r->best_cyc) r->best_cyc = d; }
            spin_ms(dwell_ms);
        }
    r->done = gal_vid_switches() - s0;
}

static void poison_and_check(gal_land_t *r, uint32_t len)
{
    /* the first and the last 4 KB of the window are checked against a normal read of the same bytes into core RAM */
    for (uint32_t k = 0; k < 2u; k++) {
        const uint32_t off = k ? len - 4096u : 0u, ha = GAL_LAND_BASE + off / 2u;
        if (!k || len > 4096u) {
            if (!slot_read_blocking(GAL_SLOT, off, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) { r->bad = 0xFFFFFFFFu; return; }
            const volatile uint32_t *ref = GAL_TAGWIN;
            for (uint32_t i = 0; i < 1024u; i++) {
                uint32_t got = 0;
                if (gfx_mb_read(ha + 2u * i, &got)) { r->bad++; continue; }
                r->checked++;
                if (got != ref[i]) r->bad++;
            }
        }
    }
}

void gal_land_test(gal_land_t *r, uint32_t len)
{
    for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0;
    r->len = len;
    if (len < 4096u || len > 131072u) return;
    for (uint32_t k = 0; k < 2u; k++)                                    /* poison the two checked regions: a copy left by an earlier run must not pass */
        for (uint32_t i = 0; i < 1024u; i++) gfx_mb_write(GAL_LAND_BASE + (k ? (len - 4096u) / 2u : 0u) + 2u * i, 0xA5A5A5A5u);
    while (!(REG_RD(R_LAND_STATUS) & 1u)) { }                            /* idle before the command */
    REG_WR(R_LAND_BASE, GAL_LAND_BASE); REG_WR(R_LAND_CTRL, 1u);
    r->started = 1u;
    const uint32_t t0 = cycles();
    r->cmd_ok = (uint32_t)slot_read_blocking(GAL_SLOT, 0u, 0x40000000u, len, CLK_HZ * 4u);
    r->cmd_cyc = cycles() - t0;
    const uint32_t t1 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t1) < CLK_HZ / 20u) { }
    r->idle_cyc = cycles() - t1; r->idle_ok = (REG_RD(R_LAND_STATUS) & 1u) ? 1u : 0u;
    r->words = REG_RD(R_LAND_WORDS); r->ovf = REG_RD(R_LAND_OVF);
    if (r->cmd_ok && r->idle_ok) poison_and_check(r, len);
}

void gal_plane_verify(gal_verify_t *r, uint32_t pat)
{
    static uint8_t line[GAL_LINE_BYTES];
    for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0;
    for (uint32_t y = 0; y < GAL_H && r->timeouts < 8u; y++) {
        gal_line(pat, y, line);
        const uint32_t a = GAL_PLANE_BASE + y * GAL_LINE_WORDS16;
        for (uint32_t k = 0; k < GAL_LINE_WORDS32; k++) {
            uint32_t got = 0; const uint32_t exp = gal_pack32(line, k);
            if (gfx_mb_read(a + 2u * k, &got)) { r->timeouts++; if (r->timeouts >= 8u) break; continue; }
            if (got != exp) { if (!r->bad) { r->first_idx = y * GAL_LINE_WORDS32 + k; r->got = got; r->exp = exp; } r->bad++; }
            r->words++;
        }
    }
}

void gal_land_map(gal_map_t *r)
{
    static uint32_t ref[GAL_MAP_BLOCKS][1024];
    for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0;
    for (uint32_t b = 0; b < GAL_MAP_BLOCKS; b++) {                       /* the reference: four normal reads, into core RAM, then copied out of the landing zone */
        if (!slot_read_blocking(GAL_SLOT, b * 4096u, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ)) { for (uint32_t b2 = 0; b2 < GAL_MAP_BLOCKS; b2++) r->match[b2] = -1; return; }
        for (uint32_t i = 0; i < 1024u; i++) ref[b][i] = GAL_TAGWIN[i];
    }
    for (uint32_t i = 0; i < GAL_MAP_BLOCKS * 1024u; i++) gfx_mb_write(GAL_LAND_BASE + 2u * i, 0xA5A5A5A5u);      /* poison the whole window */
    while (!(REG_RD(R_LAND_STATUS) & 1u)) { }
    REG_WR(R_LAND_BASE, GAL_LAND_BASE); REG_WR(R_LAND_CTRL, 1u);
    r->cmd_ok = (uint32_t)slot_read_blocking(GAL_SLOT, 0u, 0x40000000u, GAL_MAP_BLOCKS * 4096u, CLK_HZ * 4u);
    const uint32_t t1 = cycles();
    while (!(REG_RD(R_LAND_STATUS) & 1u) && (uint32_t)(cycles() - t1) < CLK_HZ / 20u) { }
    r->idle_ok = (REG_RD(R_LAND_STATUS) & 1u) ? 1u : 0u; r->words = REG_RD(R_LAND_WORDS); r->ovf = REG_RD(R_LAND_OVF);
    static uint32_t got[1024];
    for (uint32_t j = 0; j < GAL_MAP_BLOCKS; j++) {
        uint32_t same[GAL_MAP_BLOCKS] = { 1, 1, 1, 1 }, poison = 1, zero = 1;
        for (uint32_t i = 0; i < 1024u; i++) {
            uint32_t w = 0; gfx_mb_read(GAL_LAND_BASE + j * 2048u + 2u * i, &w); got[i] = w;
            for (uint32_t b = 0; b < GAL_MAP_BLOCKS; b++) if (w != ref[b][i]) same[b] = 0;
            if (w != 0xA5A5A5A5u) poison = 0;
            if (w) zero = 0;
        }
        r->match[j] = same[j] ? (int8_t)j : -1;
        for (uint32_t b = 0; b < GAL_MAP_BLOCKS && r->match[j] < 0; b++) if (same[b]) r->match[j] = (int8_t)b;
        if (r->match[j] < 0) r->match[j] = poison ? -2 : zero ? -3 : -1;
        if (r->match[j] != (int8_t)j && !r->bad_word && !r->bad_block) {         /* the first block that is not its own file block: where and what */
            r->bad_block = j + 1u;
            for (uint32_t i = 0; i < 1024u; i++) if (got[i] != ref[j][i]) { r->bad_word = i; r->got = got[i]; r->exp = ref[j][i]; break; }
        }
    }
}

volatile uint32_t gal_irq_count[4];
void irq_handler(uint32_t pend)                                          /* overrides the weak one of start.S: count each source, acknowledge what the CSR showed */
{
    for (uint32_t b = 0; b < 4u; b++) if (pend & (1u << b)) gal_irq_count[b]++;
    REG_WR(R_IRQ_PEND, pend);
}
static void irq_clear(void) { for (uint32_t b = 0; b < 4u; b++) gal_irq_count[b] = 0; REG_WR(R_IRQ_PEND, 0xFu); }

void gal_irq_test(gal_irq_t *r)
{
    for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0;
    irq_clear(); irq_enable(2u);                                         /* vsync, counted over one second */
    spin_ms(1000u); irq_disable(); r->vsync = gal_irq_count[1]; r->vsync_win_ms = 1000u;
    irq_clear(); irq_enable(1u);                                         /* data-slot command done: one per command */
    r->cmd_n = 16u;
    for (uint32_t i = 0; i < r->cmd_n; i++) slot_read_blocking(GAL_SLOT, (i & 7u) * 4096u, (uint32_t)(uintptr_t)_tag_start, 4096u, CLK_HZ);
    spin_ms(5u); irq_disable(); r->cmd = gal_irq_count[0];
    irq_clear(); irq_enable(8u);                                         /* a completed mode switch: two for a round trip */
    r->sw_n = 2u;
    gal_view_on(); spin_ms(100u); gal_view_off(); spin_ms(100u);
    irq_disable(); r->sw = gal_irq_count[3];
}
