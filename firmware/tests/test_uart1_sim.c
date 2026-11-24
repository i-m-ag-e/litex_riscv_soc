// =============================================================================
// test_uart1_sim.c — Headless UART1 loopback bring-up test (for RTL sim)
// =============================================================================
// The testbench (tb_uart1_loopback.v) ties uart1_tx -> uart1_rx, so every byte
// UART1 transmits is received back. This firmware sends 8 bytes in polling mode
// and checks each one reads back unchanged. UART1 runs at 115200 baud.
//
// portb_o signalling (watched by the testbench):
//   0x33 = firmware entered
//   0xAA = PASS (all 8 bytes echoed correctly)
//   0xF0 = FAIL (mismatch, or no byte came back)
// =============================================================================
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

int main(void) {
    portb_oe_write(0xFF);
    portb_out_write(0x33);                    // firmware entered

    uart1_ev_enable_write(0);                 // polling mode, no IRQ
    while (!uart1_rxempty_read()) uart1_rxtx_read();   // flush any stale RX

    int ok = 1;
    for (int i = 0; i < 8; i++) {
        uint8_t tx = (uint8_t)(0x41 + i);
        uart1_rxtx_write(tx);                 // send

        // Wait for the looped-back byte. The guard bounds a totally-dead link
        // so we fail cleanly (0xF0) instead of relying only on the tb watchdog.
        uint32_t guard = 0;
        while (uart1_rxempty_read()) {
            if (++guard > 2000000u) { ok = 0; break; }
        }
        if (!ok) break;

        if (uart1_rxtx_read() != tx) { ok = 0; break; }
    }

    portb_out_write(ok ? 0xAA : 0xF0);
    while (1) { }
    return 0;
}
