// =============================================================================
// test_i2c_sim.c — Headless I2C-master bring-up test (for RTL sim)
// =============================================================================
// Drives the LiteI2C master against an I2C SLAVE MODEL in the testbench
// (tb_i2c.v, address 0x42). The slave ACKs its address, stores the last byte
// written to it, and returns that stored byte on reads — so a write-then-read is
// a loopback through the slave. A write to a different address must NACK.
//
// Sequence:
//   1. write 0x5A to 0x42, read back -> expect 0x5A
//   2. write 0xC3 to 0x42, read back -> expect 0xC3
//   3. write to 0x43 (no slave)      -> expect NACK
//
// portb_o signalling (watched by the testbench):
//   0x33 = firmware entered
//   0xAA = PASS (all steps as expected)
//   0xF0 = FAIL (unexpected NACK, wrong readback, or missing NACK)
//
// I2C runs in Fast-Mode-Plus (~1 MHz) to keep the simulation short.
// =============================================================================
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>

#define SLAVE_ADDR  0x42
#define WRONG_ADDR  0x43

// Wait for the end-of-transaction status word to appear in the RX FIFO.
// Returns the NACK flag (0=ACK, 1=NACK), or -1 on timeout.
static int i2c_wait_done(void) {
    for (volatile int i = 0; i < 2000000; i++) {
        uint32_t s = i2c_master_status_read();
        if (s & (1 << CSR_I2C_MASTER_STATUS_RX_READY_OFFSET))
            return (s >> CSR_I2C_MASTER_STATUS_NACK_OFFSET) & 1;
    }
    return -1;
}

// Single-byte write. Returns 0 on ACK, 1 on NACK, -1 on timeout.
static int i2c_write_byte(uint8_t addr, uint8_t val) {
    i2c_master_addr_write(addr);
    i2c_master_settings_write(1 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET);
    i2c_master_rxtx_write(val);          // triggers the transaction
    int nack = i2c_wait_done();
    (void)i2c_master_rxtx_read();        // pop the status word from the RX FIFO
    return nack;
}

// Single-byte read. Returns the byte (0..255), or -1 on NACK/timeout.
static int i2c_read_byte(uint8_t addr) {
    i2c_master_addr_write(addr);
    i2c_master_settings_write(1 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET);
    i2c_master_rxtx_write(0x00);         // triggers the read transaction
    int nack = i2c_wait_done();
    uint8_t d = i2c_master_rxtx_read();  // pop the received byte
    if (nack != 0) return -1;
    return d;
}

int main(void) {
    portb_oe_write(0xFF);
    portb_out_write(0x33);                   // firmware entered

    i2c_phy_speed_mode_write(2);             // Fast-Mode-Plus (~1 MHz) for a short sim
    i2c_master_ev_enable_write(0);           // polling, no IRQ
    i2c_master_active_write(1);              // enable the master

    int ok = 1;

    // 1. write 0x5A, read back
    if (i2c_write_byte(SLAVE_ADDR, 0x5A) != 0) ok = 0;   // must ACK
    if (i2c_read_byte(SLAVE_ADDR)        != 0x5A) ok = 0;

    // 2. write 0xC3, read back
    if (i2c_write_byte(SLAVE_ADDR, 0xC3) != 0) ok = 0;
    if (i2c_read_byte(SLAVE_ADDR)        != 0xC3) ok = 0;

    // 3. wrong address must NACK
    if (i2c_write_byte(WRONG_ADDR, 0x11) != 1) ok = 0;   // must NACK

    portb_out_write(ok ? 0xAA : 0xF0);
    while (1) { }
    return 0;
}
