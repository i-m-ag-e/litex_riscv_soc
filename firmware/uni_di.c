#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

// --- ATMEGA TIMER0 POINTERS ---
// Mapped to the memory bus, not the CSR bus.
#define TIMER0_TCCR0 (*(volatile unsigned int *)(ATMEGA_TIMER_BASE + 0x00))
#define TIMER0_TCNT0 (*(volatile unsigned int *)(ATMEGA_TIMER_BASE + 0x04))
#define TIMER0_OCR0A (*(volatile unsigned int *)(ATMEGA_TIMER_BASE + 0x08))
#define TIMER0_TIFR0 (*(volatile unsigned int *)(ATMEGA_TIMER_BASE + 0x0C))

// RP2040 I2C address (7-bit)
#define RP2040_ADDR 0x42

// ─── UART PRINT FUNCTIONS ───
void print_char(char c) {
  while (uart_txfull_read())
    ;
  uart_rxtx_write(c);
}

void print_str(const char *str) {
  while (*str)
    print_char(*str++);
}

void print_hex8(unsigned char val) {
  const char hex_chars[] = "0123456789ABCDEF";
  print_char(hex_chars[(val >> 4) & 0x0F]);
  print_char(hex_chars[val & 0x0F]);
}

// ─── FREQUENCY-CALIBRATED I2C DELAY ───
// Targets ~5us per half-period for ~100kHz I2C bus speed.
// Assumes each loop iteration is roughly 4 CPU cycles on PicoRV32.
#define I2C_HALF_PERIOD_US 5
#define I2C_DELAY_LOOPS                                                        \
  ((CONFIG_CLOCK_FREQUENCY / 1000000) * I2C_HALF_PERIOD_US / 4)

void i2c_delay(void) {
  for (volatile unsigned int i = 0; i < I2C_DELAY_LOOPS; i++)
    ;
}

// ─── BARE-METAL I2C BITBANG DRIVER ───
// SDA is open-drain:
// Pull low by asserting OE=1, SDA=0.
// Release high by asserting OE=0 (line pulled up externally).
void i2c_out(int scl, int sda) {
  unsigned int val = 0;

  if (scl) {
    val |= (1 << CSR_I2C_W_SCL_OFFSET);
  }

  if (sda == 0) {
    // Actively pull SDA low
    val |= (1 << CSR_I2C_W_OE_OFFSET);
  }

  i2c_w_write(val);
  i2c_delay();
}

// Read the current state of SDA from the input register.
static inline int i2c_sda_read(void) {
  return (i2c_r_read() >> CSR_I2C_R_SDA_OFFSET) & 1;
}

void i2c_start(void) {
  i2c_out(1, 1); // Both high (idle)
  i2c_out(1, 0); // SDA falls while SCL high -> START
  i2c_out(0, 0); // Pull SCL low to begin clocking
}

void i2c_stop(void) {
  i2c_out(0, 0); // Ensure SDA is low
  i2c_out(1, 0); // Raise SCL while SDA still low
  i2c_out(1, 1); // SDA rises while SCL high -> STOP
}

// Transmit one byte.
// Returns 0 if slave ACKed (SDA pulled low), 1 if NACK.
int i2c_tx(unsigned char data) {
  for (int i = 7; i >= 0; i--) {
    int bit = (data >> i) & 1;
    i2c_out(0, bit); // Set SDA while SCL low
    i2c_out(1, bit); // Raise SCL (slave samples here)
    i2c_out(0, bit); // Lower SCL
  }

  // ACK phase
  i2c_out(0, 1);             // SCL low, release SDA
  i2c_out(1, 1);             // Raise SCL
  int nack = i2c_sda_read(); // Sample SDA
  i2c_out(0, 1);             // Lower SCL, keep SDA released
  return nack;
}

// Receive one byte.
// send_nak: 1 = send NACK after byte (last byte), 0 = send ACK (more bytes
// follow).
unsigned char i2c_rx(int send_nak) {
  unsigned char data = 0;

  for (int i = 7; i >= 0; i--) {
    i2c_out(0, 1); // SCL low, release SDA for slave to drive
    i2c_out(1, 1); // Raise SCL
    if (i2c_sda_read()) {
      data |= (1 << i);
    }
    i2c_out(0, 1); // Lower SCL
  }

  // ACK/NACK phase
  int nak_bit = send_nak ? 1 : 0;
  i2c_out(0, nak_bit); // Master drives SDA to ACK or NACK
  i2c_out(1, nak_bit); // Raise SCL
  i2c_out(0, 1);       // Lower SCL, release SDA

  return data;
}

// ─── TIMER HELPERS ───
// Reset and arm the timer for one compare-match cycle.
void timer_arm(void) {
  TIMER0_TCCR0 = 0x00; // Stop timer
  TIMER0_TCNT0 = 0x00; // Reset counter
  TIMER0_TIFR0 = 0x01; // Clear any pending OCF0A flag (write-1-to-clear)
  TIMER0_TCCR0 = 0x01; // Start timer (no prescaler)
}

// Block until the compare-match flag fires.
void timer_wait(void) {
  while ((TIMER0_TIFR0 & 0x01) == 0)
    ;
}

// ─── TEST PATTERNS ───
static const unsigned char test_patterns[] = {0x00, 0xFF, 0xA5,
                                              0x5A, 0x0F, 0xF0};
#define NUM_PATTERNS (sizeof(test_patterns) / sizeof(test_patterns[0]))

// ─── MAIN APP ───
void main(void) {
  unsigned char count = 0;
  unsigned int nack_count = 0;
  unsigned int tx_count = 0;

  // Release I2C bus
  i2c_out(1, 1);

  // Set compare value for ~1 second delay.
  TIMER0_OCR0A = CONFIG_CLOCK_FREQUENCY;

  print_str("\r\n================================\r\n");
  print_str("  FPGA I2C MASTER BROADCAST\r\n");
  print_str("  Target: RP2040 @ 0x");
  print_hex8(RP2040_ADDR);
  print_str("\r\n================================\r\n\n");

  while (1) {
    // ── Phase 1: Send incrementing counter byte ──
    i2c_start();
    int nack_addr = i2c_tx((RP2040_ADDR << 1) | 0);

    if (nack_addr) {
      print_str("[WARN] Address NACK (no device at 0x");
      print_hex8(RP2040_ADDR);
      print_str(")\r\n");
      i2c_stop();
      nack_count++;
    } else {
      int nack_data = i2c_tx(count);
      i2c_stop();
      tx_count++;

      print_str("TX #");
      print_hex8((tx_count >> 8) & 0xFF);
      print_hex8(tx_count & 0xFF);
      print_str(" -> 0x");
      print_hex8(count);
      print_str(nack_data ? "  [NACK]" : "  [ACK] ");
      print_str("\r\n");

      if (nack_data)
        nack_count++;
    }

    // ── Phase 2: Send fixed test patterns ──
    for (unsigned int p = 0; p < NUM_PATTERNS; p++) {
      i2c_start();
      if (!i2c_tx((RP2040_ADDR << 1) | 0)) {
        int r = i2c_tx(test_patterns[p]);
        print_str("  Pattern 0x");
        print_hex8(test_patterns[p]);
        print_str(r ? " NACK\r\n" : " ACK\r\n");
      }
      i2c_stop();
    }

    // ── Phase 3: Read one byte back ──
    i2c_start();
    if (!i2c_tx((RP2040_ADDR << 1) | 1)) {
      unsigned char rxbyte = i2c_rx(1); // Read 1 byte, send NACK
      i2c_stop();
      print_str("  RX from RP2040: 0x");
      print_hex8(rxbyte);
      print_str("\r\n");
    } else {
      i2c_stop();
      print_str("  RX: Address NACK\r\n");
    }

    // ── Summary every 16 cycles ──
    if ((count & 0x0F) == 0x0F) {
      print_str("  [STATS] TX=");
      print_hex8((tx_count >> 8) & 0xFF);
      print_hex8(tx_count & 0xFF);
      print_str(" NACK=");
      print_hex8((nack_count >> 8) & 0xFF);
      print_hex8(nack_count & 0xFF);
      print_str("\r\n");
    }

    count++;

    // ── Delay via custom ATMEGA timer ──
    timer_arm();
    timer_wait();

    print_str("\r\n");
  }
}