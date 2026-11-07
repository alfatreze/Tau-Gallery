#ifndef SD_PROBE_H
#define SD_PROBE_H
#include <stdint.h>
/* M-1 SD feed probe (ported from Tau V V-011 dg_sd_speed, reworked for fw2/stream/slot.c).
 * Commands are at most 4096 bytes: the landing zone (GALLERY_ANALYSIS 12.7 A3). Everything blocks; run with no track playing. */
typedef struct {
    uint32_t n;          /* commands completed */
    uint32_t bytes;      /* bytes read */
    uint32_t cycles;     /* total elapsed clk cycles */
    uint32_t kbps;       /* sustained KB/s (1 KB = 1000 B, as the V-011 number) over everything measured */
    uint32_t worst_cyc;  /* slowest single command */
    uint32_t best_cyc;   /* fastest single command */
    uint32_t hist[8];    /* per-command time: <1ms <2 <3 <4 <5 <8 <13 >=13 ms */
    uint32_t p1_n, p1_cyc, p1_worst;   /* the first pass over the span alone (what the cache has not seen) */
    uint32_t fail_at;    /* byte offset + 1 of the first failed command, 0 when none failed */
    int      ok;         /* 1 = every command succeeded; statistics are filled whenever n > 0 */
} sd_probe_t;
/* One slot, `span` bytes, wrapping back to 0 when the end is reached, for about `ms` milliseconds (at least one pass). */
void sd_probe_run(sd_probe_t *r, uint32_t slot, uint32_t span, uint32_t chunk, uint32_t dst_off, uint32_t ms);
/* Two slots alternately (A, B, A, B ...), each at its own advancing offset: the cost of a slot switch on every command. */
void sd_probe_switch(sd_probe_t *r, uint32_t slot_a, uint32_t span_a, uint32_t slot_b, uint32_t span_b, uint32_t chunk, uint32_t dst_off, uint32_t ms);
#endif
