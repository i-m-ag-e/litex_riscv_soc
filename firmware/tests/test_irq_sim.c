// =============================================================================
// test_irq_sim.c — Headless interrupt-path bring-up test (for RTL sim)
// =============================================================================
// Proves the picorv32 interrupt path end-to-end with NO external stimulus:
// enable the timer0 periodic interrupt and wait for the CPU's ISR (isr.c) to
// run. Reaching the pass code requires all of: irq enable (irq_setie), the
// interrupt mask, the PROGADDR_IRQ vector, ISR execution (system_ticks++), and
// a correct return-from-interrupt so main() resumes.
//
// portb_o signalling (watched by the testbench):
//   0x33 = firmware entered
//   0xAA = PASS (ISR ran several times -> IRQ path works)
//
// If the IRQ path is broken, system_ticks stays 0, the firmware never writes
// 0xAA, and the testbench watchdog reports TIMEOUT FAIL. (system_ticks is
// incremented only inside isr(), so it is a genuine proof the ISR executed.)
// =============================================================================
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

int main(void) {
    portb_oe_write(0xFF);
    portb_out_write(0x33);                    // firmware entered

    // Fast periodic timer0 tick (~10 us) so the test converges quickly in sim.
    timer0_en_write(0);
    timer0_load_write(0);
    timer0_reload_write(CONFIG_CLOCK_FREQUENCY / 100000);  // ~1000 cycles
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(CSR_TIMER0_EV_ENABLE_ZERO_SIZE);
    timer0_en_write(1);

    irq_setmask(1 << TIMER0_INTERRUPT);
    irq_setie(1);

    // Block until the ISR has demonstrably run several times.
    while (system_ticks < 5) { }

    portb_out_write(0xAA);                     // IRQ path proven
    while (1) { }
    return 0;
}
