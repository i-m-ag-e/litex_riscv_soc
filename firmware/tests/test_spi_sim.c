// =============================================================================
// test_spi_sim.c — Headless SPI-master loopback bring-up test (for RTL sim)
// =============================================================================
// The testbench (tb_spi_loopback.v) ties spi_mosi -> spi_miso, so whatever the
// LiteSPI generic master shifts out comes straight back in. This firmware does
// a few polled 8-bit transfers and checks the readback equals the sent byte.
//
// portb_o signalling (watched by the testbench):
//   0x33 = firmware entered
//   0xAA = PASS (every byte read back correctly)
//   0xF0 = FAIL (a mismatch)
//
// No UART prints, no menu, no external stimulus — pure GPIO pass/fail so the
// test runs headless and stays independent of the console UART.
// =============================================================================
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

// One polled 8-bit transfer: assert CS, launch, wait RX_READY, read, drop CS.
static uint8_t spi_xfer(uint8_t b) {
    spi_master_cs_write(1);                 // assert CS0
    spi_master_rxtx_write(b);               // start the 8-bit shift
    while (!(spi_master_ev_pending_read() &
             (1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET)));
    uint8_t r = spi_master_rxtx_read();     // byte shifted back in on MISO
    spi_master_cs_write(0);                 // deassert CS
    spi_master_ev_pending_write(1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET);
    return r;
}

int main(void) {
    portb_oe_write(0xFF);
    portb_out_write(0x33);                   // firmware entered

    // 8-bit length, 1-bit (single) width, mask 1 — same config the on-bench
    // test_spi_loopback.c uses.
    spi_master_phyconfig_write(
        (8 << CSR_SPI_MASTER_PHYCONFIG_LEN_OFFSET) |
        (1 << CSR_SPI_MASTER_PHYCONFIG_WIDTH_OFFSET) |
        (1 << CSR_SPI_MASTER_PHYCONFIG_MASK_OFFSET));

    // Rotation-sensitive patterns: if the PHY sampled MISO on the wrong edge and
    // returned a bit-rotated byte, these would not compare equal.
    static const uint8_t tv[5] = {0x41, 0xA5, 0x5A, 0x3C, 0xFF};
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        if (spi_xfer(tv[i]) != tv[i]) ok = 0;
    }

    portb_out_write(ok ? 0xAA : 0xF0);
    while (1) { }
    return 0;
}
