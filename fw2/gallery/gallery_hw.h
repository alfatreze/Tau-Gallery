/* Tau Gallery M0 hardware tests (docs/M0_DESIGN.md 6c): the viewer plane, the mode switch, reads of 4 KB to 128 KB through the landing path, and the interrupt block. All blocking, run from the test page. */
#ifndef GALLERY_HW_H
#define GALLERY_HW_H
#include <stdint.h>
#include "gallery_pattern.h"
#define GAL_PLANE_BASE 0x200000u        /* SDRAM halfword address of plane line 0 (4 MiB: above both frame buffers, which end at 2 MiB) */
#define GAL_LAND_BASE  0x400000u        /* SDRAM halfword address of the landing window (8 MiB) */
#define GAL_SLOT       6u               /* the data slot read by the landing and interrupt tests: tau-cold.bin, a deferload file of at least 147 KB */
#define GAL_LAND_KINDS 4u
extern const uint32_t gal_land_len[GAL_LAND_KINDS];                   /* 4 KB, 16 KB, 64 KB, 128 KB */

/* Writes the whole plane through the SDRAM mailbox; progress(y) is called every 30 lines (may be 0). Returns the number of mailbox writes that timed out (0 = all landed). */
uint32_t gal_fill(uint32_t pat, void (*progress)(uint32_t y));
/* The viewer: points the scan-out at the plane and asks for the viewer / the UI. Each waits up to 0.5 s for the mode on the bus and returns the wait in clk cycles, or 0 on timeout. */
uint32_t gal_view_on(void);
uint32_t gal_view_off(void);
uint32_t gal_vid_switches(void);                                        /* completed switches (R_VID_MODE [31:16]) */

typedef struct { uint32_t n, ok, done, worst_cyc, best_cyc; } gal_sw_t;
void gal_switch_test(gal_sw_t *r, uint32_t round_trips, uint32_t dwell_ms);   /* UI to viewer to UI, round_trips times; the plane must be filled */

typedef struct {
    uint32_t len, started;          /* bytes asked for; 1 once the command was issued */
    uint32_t cmd_ok, cmd_cyc;       /* the target read completed with result 0; clk cycles it took */
    uint32_t idle_cyc, idle_ok;     /* clk cycles from the command's end until the landing path reported idle; idle within 50 ms */
    uint32_t words, ovf;            /* bridge words accepted, words dropped (FIFO full) */
    uint32_t checked, bad;          /* 32-bit words compared against a normal read (the first and the last 4 KB), and how many differed */
} gal_land_t;
void gal_land_test(gal_land_t *r, uint32_t len);

typedef struct { uint32_t vsync, vsync_win_ms, cmd, cmd_n, sw, sw_n; } gal_irq_t;
void gal_irq_test(gal_irq_t *r);                                         /* vsync interrupts over 1 s, one per data-slot command (16 of 4 KB), one per mode switch */
extern volatile uint32_t gal_irq_count[4];
uint32_t gal_kbps(uint32_t bytes, uint32_t cyc);
#endif
