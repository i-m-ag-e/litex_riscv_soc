#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

// --- Console Helpers ---
void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

void print_str(const char *str) {
    while(*str) print_char(*str++);
}

void print_hex32(uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        print_char(hex_chars[(val >> i) & 0x0F]);
    }
}

char read_char_blocking(void) {
    while(uart_rxempty_read());
    return uart_rxtx_read();
}

// =============================================================================
// TEST 1: UART1 POLLING (High-Speed FIFO Stress Test)
// =============================================================================
void test_uart1_polling(void) {
    // 1. Force Interrupts OFF for this test
    uart1_ev_enable_write(0);

    uint32_t bytes_to_test = 10000;
    uint32_t tx_count = 0;
    uint32_t rx_count = 0;
    uint32_t errors = 0;
    
    uint8_t next_tx_byte = 0;
    uint8_t expected_rx_byte = 0;

    print_str("\r\n[POLLING] Flushing FIFOs...\r\n");
    while(!uart1_rxempty_read()) { uart1_rxtx_read(); }

    print_str("[POLLING] Blasting 10,000 bytes at full duplex...\r\n");

    while (rx_count < bytes_to_test) {
        // Fill TX FIFO
        while (!uart1_txfull_read() && tx_count < bytes_to_test) {
            uart1_rxtx_write(next_tx_byte++);
            tx_count++;
        }
        // Drain RX FIFO
        while (!uart1_rxempty_read()) {
            uint8_t received = uart1_rxtx_read();
            if (received != expected_rx_byte) errors++;
            expected_rx_byte++;
            rx_count++;
        }
    }

    print_str("\r\n=== UART1 POLLING RESULTS ===\r\n");
    print_str("Bytes Sent:     "); print_hex32(tx_count); print_str("\r\n");
    print_str("Bytes Received: "); print_hex32(rx_count); print_str("\r\n");
    print_str("Data Errors:    "); print_hex32(errors);   print_str("\r\n");
    if (errors == 0 && tx_count == bytes_to_test) {
        print_str(">> PASS (Polling)\r\n");
    } else {
        print_str(">> FAIL: Check jumper between B15 and B16.\r\n");
    }
}

// =============================================================================
// TEST 2: UART1 INTERRUPTS (Event-Driven Loopback)
// =============================================================================
void test_uart1_interrupt(void) {
    print_str("\r\n[IRQ] Flushing FIFOs...\r\n");
    while(!uart1_rxempty_read()) { uart1_rxtx_read(); }
    uart1_rx_flag = 0;

    // 1. Force Interrupts ON for this test
    uart1_ev_pending_write(uart1_ev_pending_read()); // Clear stale events
    uart1_ev_enable_write(1 << CSR_UART1_EV_ENABLE_RX_OFFSET);

    print_str("[IRQ] Sending characters A-Z. Waiting for CPU ISR to catch them...\r\n");

    uint32_t errors = 0;
    for (char c = 'A'; c <= 'Z'; c++) {
        uart1_rxtx_write(c); // Send the character
        
        // Wait for the ISR to set the flag (with a 500ms timeout)
        uint32_t timeout = system_ticks + 500;
        while (!uart1_rx_flag && (system_ticks < timeout)) {
            // CPU spins here, ISR will interrupt this loop
        }

        if (uart1_rx_flag) {
            uart1_rx_flag = 0; // Reset flag for next letter
            print_str("  [ISR Caught]: "); print_hex32(uart1_rx_byte); print_str("\r\n");
            if (uart1_rx_byte != c) errors++;
        } else {
            print_str("  [!] TIMEOUT: Interrupt never fired!\r\n");
            errors++;
            break;
        }
        delay_ms(50); // Small delay so you can watch it scroll
    }

    if (errors == 0) {
        print_str(">> PASS (Interrupts)\r\n");
    } else {
        print_str(">> FAIL: Interrupt routing issue or jumper disconnected.\r\n");
    }

    // Disable interrupts again so they don't fire randomly after the test
    uart1_ev_enable_write(0); 
}

// =============================================================================
// SYSTEM INIT & MAIN MENU
// =============================================================================
void system_init(void) {
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); timer0_load_write(0); timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(CSR_TIMER0_EV_ENABLE_ZERO_SIZE); 
    timer0_en_write(1);

    portb_oe_write(0xFF); 
    portc_oe_write(0x00); 

    // Add Timer0 and UART1 to the CPU's global interrupt mask
    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << UART1_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    while(1) {
        print_str("\r\n==================================\r\n");
        print_str(" UART1 VALIDATION MENU\r\n");
        print_str(" Ensure JB Pins B15 & B16 are tied\r\n");
        print_str("==================================\r\n");
        print_str(" 1: Polling Test (10,000 bytes)\r\n");
        print_str(" 2: Interrupt Test (A-Z slowly)\r\n");
        print_str(" Select Test: ");
        
        char choice = read_char_blocking();
        print_char(choice); // Echo choice
        print_str("\r\n");

        if (choice == '1') {
            test_uart1_polling();
        } else if (choice == '2') {
            test_uart1_interrupt();
        } else {
            print_str(" Invalid selection.\r\n");
        }
    }
    return 0;
}
