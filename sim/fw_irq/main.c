/* Firmware-in-the-loop test of the Tau Gallery interrupt block (tau_soc.v IRQ_ENABLE, R_IRQ_PEND 0x214) and of the external-interrupt path of the PRODUCTION start-up code
 * (fw2/platform/start.S: trap_entry, irq_entry, irq_enable, irq_disable), run by the real VexRiscv. sim/tb_irq_soc.v pulses the sources after each "P<n>" line:
 *   P1: bit 0 (data-slot command done) with interrupts off: polling sees it, a write of 1 clears it.
 *   P2: interrupts on for bit 0, three pulses while mix() runs; the handler counts and acknowledges; the checksum must equal the one computed with interrupts off.
 *   P3: interrupts globally on but bit 0 masked: the pulse sets the pending bit and does not interrupt.
 *   P4: bit 2 (landing path idle) pending while only bit 0 is unmasked: no interrupt. P5: bit 2 unmasked and a new edge: one interrupt, the handler sees 4.
 * Every check prints "IRQ ok n" or "IRQ FAIL n"; the last line is "IRQ DONE". Mutants (Makefile test-rtl-irq-soc) edit a copy of start.S and must make this fail. */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_IRQ_PEND 0x80000214u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "IRQ ok " : "IRQ FAIL "); num_(n); putc_('\n'); }
uint32_t *wd_jb_ptr;                       /* read by wd_resume (start.S): the watchdog is not used here, but the production start-up code references both */
volatile uint32_t wd_cut_flag;
extern uint32_t mix(uint32_t n);
extern void irq_enable(uint32_t mask), irq_disable(void);
volatile uint32_t irq_count, irq_last;
void irq_handler(uint32_t pend)
{
    irq_count = irq_count + 1u; irq_last = pend; REG(R_IRQ_PEND) = pend;               /* acknowledge what the CSR showed */
    /* a handler is allowed to use every caller-saved register: make it do so, so that a start-up code that fails to restore one is caught */
    __asm__ volatile("li t0, -1\n li t1, -1\n li t2, -1\n li t3, -1\n li t4, -1\n li t5, -1\n li t6, -1\n"
                     "li a1, -1\n li a2, -1\n li a3, -1\n li a4, -1\n li a5, -1\n li a6, -1\n li a7, -1\n"
                     ::: "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "a2", "a3", "a4", "a5", "a6", "a7");
}
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }
int main(void)
{
    chk(1, REG(R_IRQ_PEND) == 0u);
    /* P1: polling */
    puts_("P1\n");
    { uint32_t n = 0; while (!(REG(R_IRQ_PEND) & 1u) && n < 200000u) n++; chk(2, (REG(R_IRQ_PEND) & 1u) != 0u); }
    REG(R_IRQ_PEND) = 1u; spin(20);
    chk(3, REG(R_IRQ_PEND) == 0u);
    /* P2: three interrupts during a computation that keeps every register live */
    const uint32_t golden = mix(2500u);
    irq_count = 0; irq_last = 0;
    irq_enable(1u);
    puts_("P2\n");
    const uint32_t got = mix(2500u);
    irq_disable();
    chk(4, irq_count == 3u);
    chk(5, irq_last == 1u);
    chk(6, got == golden);
    /* P3: masked */
    irq_count = 0;
    irq_enable(0u);
    puts_("P3\n");
    spin(1500u);
    chk(7, irq_count == 0u && (REG(R_IRQ_PEND) & 1u) != 0u);
    REG(R_IRQ_PEND) = 1u;
    irq_disable();
    /* P4: another source pending, not unmasked */
    irq_count = 0;
    irq_enable(1u);
    puts_("P4\n");
    spin(1500u);
    chk(8, irq_count == 0u && (REG(R_IRQ_PEND) & 4u) != 0u);
    REG(R_IRQ_PEND) = 4u;
    /* P5: unmask it and give it a new edge */
    irq_enable(4u);
    puts_("P5\n");
    spin(1500u);
    chk(9, irq_count == 1u && irq_last == 4u);
    irq_disable();
    puts_("IRQ DONE\n");
    for (;;) { }
}
