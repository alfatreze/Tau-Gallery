#pragma GCC optimize ("Os")
#include "sd_probe.h"
#include "../platform/mmio.h"
#include "../stream/slot.h"

void sd_probe_run(sd_probe_t *r, uint32_t slot, uint32_t span, uint32_t chunk, uint32_t dst_off, uint32_t ms)
{
    for (uint32_t i = 0; i < sizeof *r / 4u; i++) ((uint32_t *)r)[i] = 0;
    r->best_cyc = 0xFFFFFFFFu;
    if (!chunk || chunk > 4096u || span < chunk) return;
    const uint32_t ms_cyc = CLK_HZ / 1000u;
    const uint32_t limit = ms_cyc * ms, tmo = CLK_HZ;            /* 1 s per command */
    static const uint8_t edge[7] = { 1, 2, 3, 4, 5, 8, 13 };
    uint32_t t0 = cycles();
    while ((r->n + 1u) * chunk <= span && (uint32_t)(cycles() - t0) < limit) {
        uint32_t t = cycles();
        if (!slot_read_blocking(slot, r->n * chunk, dst_off, chunk, tmo)) { r->fail_at = r->n * chunk + 1u; break; }   /* keep what was measured */
        uint32_t d = cycles() - t;
        if (d > r->worst_cyc) r->worst_cyc = d;
        if (d < r->best_cyc) r->best_cyc = d;
        uint32_t b = 0; while (b < 7u && d >= edge[b] * ms_cyc) b++;
        r->hist[b]++;
        r->n++;
    }
    r->cycles = cycles() - t0;
    r->bytes = r->n * chunk;
    if (!r->n || r->cycles < 1000u) return;
    r->kbps = (uint32_t)(((uint64_t)r->bytes * CLK_HZ) / ((uint64_t)r->cycles * 1000u));
    r->ok = r->fail_at == 0u;
}
