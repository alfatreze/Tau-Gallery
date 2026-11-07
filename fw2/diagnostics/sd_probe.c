#pragma GCC optimize ("Os")
#include "sd_probe.h"
#include "../platform/mmio.h"
#include "../stream/slot.h"

static void clear(sd_probe_t *r) { for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0; r->best_cyc = 0xFFFFFFFFu; }
static void note(sd_probe_t *r, uint32_t d)
{
    static const uint8_t edge[7] = { 1, 2, 3, 4, 5, 8, 13 };
    const uint32_t ms_cyc = CLK_HZ / 1000u;
    if (d > r->worst_cyc) r->worst_cyc = d;
    if (d < r->best_cyc) r->best_cyc = d;
    uint32_t b = 0; while (b < 7u && d >= edge[b] * ms_cyc) b++;
    r->hist[b]++; r->n++;
}
static void finish(sd_probe_t *r, uint32_t t0, uint32_t chunk)
{
    r->cycles = cycles() - t0;
    r->bytes = r->n * chunk;
    if (!r->n || r->cycles < 1000u) return;
    r->kbps = (uint32_t)(((uint64_t)r->bytes * CLK_HZ) / ((uint64_t)r->cycles * 1000u));
    r->ok = r->fail_at == 0u;
}

void sd_probe_run(sd_probe_t *r, uint32_t slot, uint32_t span, uint32_t chunk, uint32_t dst_off, uint32_t ms)
{
    clear(r);
    if (!chunk || chunk > 4096u || span < chunk) return;
    const uint32_t per = span / chunk, limit = (CLK_HZ / 1000u) * ms, tmo = CLK_HZ;
    uint32_t t0 = cycles();
    while (r->n < per || (uint32_t)(cycles() - t0) < limit) {
        const uint32_t off = (r->n % per) * chunk, t = cycles();
        if (!slot_read_blocking(slot, off, dst_off, chunk, tmo)) { r->fail_at = off + 1u; break; }
        note(r, cycles() - t);
        if (r->n == per) { r->p1_n = per; r->p1_cyc = cycles() - t0; r->p1_worst = r->worst_cyc; }
    }
    finish(r, t0, chunk);
}

void sd_probe_switch(sd_probe_t *r, uint32_t slot_a, uint32_t span_a, uint32_t slot_b, uint32_t span_b, uint32_t chunk, uint32_t dst_off, uint32_t ms)
{
    clear(r);
    if (!chunk || chunk > 4096u || span_a < chunk || span_b < chunk) return;
    const uint32_t pa = span_a / chunk, pb = span_b / chunk, limit = (CLK_HZ / 1000u) * ms, tmo = CLK_HZ;
    uint32_t t0 = cycles();
    for (uint32_t k = 0; (uint32_t)(cycles() - t0) < limit; k++)
        for (uint32_t s = 0; s < 2u; s++) {
            const uint32_t slot = s ? slot_b : slot_a, off = (k % (s ? pb : pa)) * chunk, t = cycles();
            if (!slot_read_blocking(slot, off, dst_off, chunk, tmo)) { r->fail_at = off + 1u; finish(r, t0, chunk); return; }
            note(r, cycles() - t);
        }
    finish(r, t0, chunk);
}
