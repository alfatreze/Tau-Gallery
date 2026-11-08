/* Firmware-in-the-loop test of the Tau Gallery viewer registers in tau_soc.v (VID_MODE 0x1EC, VIEW_BASE 0x1F0, VIEW_STRIDE 0x1F4, VIEW_CAPS 0x1F8), run by the real VexRiscv.
 * sim/tb_view_soc.v drives the status inputs and watches the outputs; "READY" hands over to the bench, which then changes the inputs. Every check prints "VIEW ok n" or "VIEW FAIL n".
 * Built with -DOFF=1 it checks the core without the viewer: every register reads 0 and writes change nothing. The last line is "VIEW DONE". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_VID_MODE    0x800001ECu
#define R_VIEW_BASE   0x800001F0u
#define R_VIEW_STRIDE 0x800001F4u
#define R_VIEW_CAPS   0x800001F8u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "VIEW ok " : "VIEW FAIL "); num_(n); putc_('\n'); }
int main(void)
{
#ifdef OFF
    chk(1, REG(R_VIEW_CAPS) == 0u);
    REG(R_VIEW_BASE) = 0x01234567u; REG(R_VIEW_STRIDE) = 1208u; REG(R_VID_MODE) = 1u;
    chk(2, REG(R_VID_MODE) == 0u && REG(R_VIEW_BASE) == 0u && REG(R_VIEW_STRIDE) == 0u);
    puts_("READY\n");
    for (volatile uint32_t i = 0; i < 4000u; i++) { }
    chk(3, REG(R_VID_MODE) == 0u);
#else
    chk(1, REG(R_VIEW_CAPS) == 0x80000003u);                            /* present, viewer, sequencer */
    chk(2, REG(R_VID_MODE) == (5u << 16));                              /* five switches done, nothing requested, UI on the bus */
    chk(3, REG(R_VIEW_STRIDE) == 1200u && REG(R_VIEW_BASE) == 0u);      /* reset values */
    REG(R_VIEW_BASE) = 0x01234567u; REG(R_VIEW_STRIDE) = 1208u; REG(R_VID_MODE) = 1u;
    chk(4, REG(R_VIEW_BASE) == 0x01234567u && REG(R_VIEW_STRIDE) == 1208u);
    chk(5, REG(R_VID_MODE) == ((5u << 16) | 4u));                       /* requested, not yet pending or on the bus */
    puts_("READY\n");
    for (volatile uint32_t i = 0; i < 4000u; i++) { }                   /* the bench changes the inputs meanwhile */
    chk(6, REG(R_VID_MODE) == ((6u << 16) | 4u | 2u | 1u));             /* requested, pending, viewer on the bus, six switches */
    puts_("READY2\n");
    for (volatile uint32_t i = 0; i < 4000u; i++) { }                   /* the bench now clears 'on the bus' and keeps 'pending': an asymmetric value */
    chk(9, REG(R_VID_MODE) == ((6u << 16) | 4u | 2u));
    REG(R_VIEW_BASE) = 0xFFFFFFFFu;
    chk(7, REG(R_VIEW_BASE) == 0x01FFFFFFu);                            /* 25 bits */
    REG(R_VID_MODE) = 0u;
    chk(8, (REG(R_VID_MODE) & 4u) == 0u);                               /* the request is gone */
#endif
    puts_("VIEW DONE\n");
    for (;;) { }
}
