#include <generated/csr.h>
#include <generated/soc.h>

// ─── PRIMARY UART (USB to Laptop / litex_term) ───
void term_write_char(char c) {
  while (uart_txfull_read())
    ;
  uart_rxtx_write(c);
}

void term_write_str(const char *str) {
  while (*str) {
    term_write_char(*str++);
  }
}

int term_read_char_nonblocking(char *c) {
  if (uart_rxempty_read() == 0) {
    *c = (char)uart_rxtx_read();
    uart_ev_pending_write(1 << CSR_UART_EV_PENDING_RX_OFFSET);
    return 1;
  }
  return 0;
}

// ─── SECONDARY UART (FPGA to RP2040) ───
void rp2040_write_char(char c) {
  while (uart1_txfull_read())
    ;
  uart1_rxtx_write(c);
}

int rp2040_read_char_nonblocking(char *c) {
  if (uart1_rxempty_read() == 0) {
    *c = (char)uart1_rxtx_read();
    uart1_ev_pending_write(1
                           << CSR_UART1_EV_PENDING_RX_OFFSET); // Acknowledge RX
    return 1;
  }
  return 0;
}

// ─── MAIN ROUTINE ───
int main(void) {
  term_write_str("\r\n================================\r\n");
  term_write_str("  FPGA <-> RP2040 UART BRIDGE\r\n");
  term_write_str("  Type here to send to RP2040.\r\n");
  term_write_str("================================\r\n\n");

  char c;

  while (1) {
    // 1. Laptop -> FPGA -> RP2040
    if (term_read_char_nonblocking(&c)) {
      rp2040_write_char(c);

      // Local echo to term to see what you typed
      term_write_char(c);
      if (c == '\r') {
        term_write_char('\n');
        rp2040_write_char('\n');
      }
    }

    // 2. RP2040 -> FPGA -> Laptop
    if (rp2040_read_char_nonblocking(&c)) {
      term_write_char(c);
    }
  }

  return 0;
}