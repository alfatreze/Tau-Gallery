/* Firmware-in-the-loop test of the hardware TGY decoder's MMIO window in tau_soc.v (registers 0x220-0x24C, interrupt source 4, CORE_VERSION rev 29). The real VexRiscv programs the decoder the way the gallery
 * firmware will: load the two quantisation tables, start a tile, poll STATUS, read CYCLES, check the interrupt pending bit and clear it. sim/tb_tgy_soc.v preloads the SDRAM model with the tables and the stream
 * (tile 4 of sim/gen_tgy_dec_vectors.py: 4:2:0, 48 x 48, 605 bytes), prints nothing itself but checks every line this program writes and then the picture memory byte for byte. */
#include <stdint.h>
#define REG(a)    (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_VERSION 0x80000040u
#define R_IRQ     0x80000214u
#define T_CTRL    0x80000220u
#define T_STATUS  0x80000224u
#define T_SRC     0x80000228u
#define T_LEN     0x8000022Cu
#define T_DST     0x80000230u
#define T_PITCH   0x80000234u
#define T_GEOM    0x80000238u
#define T_QY      0x8000023Cu
#define T_QC      0x80000240u
#define T_CYCLES  0x80000244u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex_(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789ABCDEF"[(v >> i) & 15]); }
static void line_(const char *tag, uint32_t v) { puts_(tag); putc_(' '); hex_(v); putc_('\n'); }
static uint32_t wait_idle(void) { uint32_t n = 0, s; do { s = REG(T_STATUS); n++; } while ((s & 1u) && n < 2000000u); return s; }
int main(void)
{
    line_("VER", REG(R_VERSION));
    line_("ST", REG(T_STATUS));                       /* present, idle */
    REG(T_SRC) = 0x1001; REG(T_LEN) = 605; REG(T_DST) = 0x8001; REG(T_PITCH) = 397; REG(T_GEOM) = (2u << 16) | (48u << 8) | 48u; REG(T_QY) = 0x200; REG(T_QC) = 0x300;
    line_("QCHK", REG(T_SRC) + REG(T_LEN) + REG(T_DST) + REG(T_PITCH) + REG(T_GEOM) + REG(T_QY) + REG(T_QC));
    REG(T_CTRL) = 2;                                  /* LOAD_TABLES */
    line_("BUSY", REG(T_STATUS));                     /* busy right after the store */
    line_("TBL", wait_idle());
    REG(T_CTRL) = 1;                                  /* START_TILE */
    line_("BUSY2", REG(T_STATUS));
    line_("TILE", wait_idle());
    line_("IRQ", REG(R_IRQ));                         /* bit 4 */
    REG(R_IRQ) = 0x10u;
    line_("IRQ2", REG(R_IRQ));                        /* hardware level still up: the bit may rise again only on a new edge, so it stays clear */
    REG(T_CTRL) = 8;                                  /* CLEAR */
    line_("ST2", REG(T_STATUS));
    line_("CYC", REG(T_CYCLES) != 0);
    puts_("TGY DONE\n");
    for (;;) ;
}
