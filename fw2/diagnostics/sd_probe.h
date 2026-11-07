#ifndef SD_PROBE_H
#define SD_PROBE_H
#include <stdint.h>
/* M-1 SD feed probe (ported from Tau V V-011 dg_sd_speed, reworked for fw2/stream/slot.c).
 * Reads `slot` sequentially in `chunk`-byte commands (4096 max: the landing zone, see GALLERY_ANALYSIS 12.7 A3) into the
 * landing offset `dst_off`, for about `ms` milliseconds or `span` bytes. Blocks; run it with nothing else using the slot. */
typedef struct {
    uint32_t n;          /* commands completed */
    uint32_t bytes;      /* bytes read */
    uint32_t cycles;     /* total elapsed clk cycles */
    uint32_t kbps;       /* sustained KB/s (1 KB = 1000 B, as the V-011 number) */
    uint32_t worst_cyc;  /* slowest single command */
    uint32_t best_cyc;   /* fastest single command */
    uint32_t hist[8];    /* per-command time: <1ms <2 <3 <4 <5 <8 <13 >=13 ms */
    uint32_t fail_at;    /* byte offset of the first failed command, 0 when none failed */
    int      ok;         /* 1 = every command succeeded; the statistics are filled whenever n > 0, even after a failure */
} sd_probe_t;
void sd_probe_run(sd_probe_t *r, uint32_t slot, uint32_t span, uint32_t chunk, uint32_t dst_off, uint32_t ms);
#endif
