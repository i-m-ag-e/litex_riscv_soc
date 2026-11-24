#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

#define RP2040_I2C_ADDR 0x42

// --- Console Helpers ---
void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

void print_str(const char *str) {
    while(*str) print_char(*str++);
}

void print_hex8(uint8_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    print_char(hex_chars[(val >> 4) & 0x0F]);
    print_char(hex_chars[val & 0x0F]);
}

char read_char_blocking(void) {
    while(uart_rxempty_read());
    return uart_rxtx_read();
}

// =============================================================================
// TEST 1 & 2: UART1 (Kept from previous step)
// =============================================================================
// (Assuming test_uart1_polling() and test_uart1_interrupt() are still here)
// ... [Keep your UART1 functions exactly as they were] ...


// =============================================================================
// TEST 3: I2C POLLING (Robust Read/Write with NACK Detection)
// =============================================================================
void test_i2c_polling(void) {
    i2c_master_ev_enable_write(0); // Force I2C Interrupts OFF

    print_str("\r\n[I2C POLLING] Flushing FIFOs...\r\n");
    while(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_RX_READY_OFFSET)) {
        i2c_master_rxtx_read(); 
    }
    i2c_master_ev_pending_write(i2c_master_ev_pending_read());

    print_str("[I2C POLLING] Attempting to write 5 bytes to Shrike (0x42)...\r\n");
    i2c_master_addr_write(RP2040_I2C_ADDR);

    uint32_t errors = 0;

    for (uint8_t i = 0; i < 5; i++) {
        i2c_master_settings_write((1 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET) | 
                                  (0 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET));
        
        uint8_t tx_val = 0xA0 + i;
        i2c_master_rxtx_write(tx_val); 

        // 1. Bypass the race condition: Wait for physical hardware to easily finish
        delay_ms(2); 

        // 2. Safety Catch: Ensure state machine has returned to idle
        uint32_t timeout = system_ticks + 100;
        while(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_TX_UNFINISHED_OFFSET)) {
            if (system_ticks > timeout) {
                print_str("  [!] BUS LOCKUP TIMEOUT\r\n");
                break;
            }
        }

        // 3. The transaction is guaranteed finished, perfectly safe to read NACK
        if (i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_NACK_OFFSET)) {
            print_str("  [!] NACK! Shrike did not acknowledge TX.\r\n");
            errors++;
        } else {
            print_str("  [TX OK] Wrote: 0x"); print_hex8(tx_val); print_str("\r\n");
        }
        delay_ms(100); // Small pause between bytes
    }

    print_str("\r\n[I2C POLLING] Attempting to read 5 bytes from Shrike...\r\n");
    for (uint8_t i = 0; i < 5; i++) {
        i2c_master_settings_write((0 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET) | 
                                  (1 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET));
        
        i2c_master_rxtx_write(0x00); // Trigger hardware clock to fetch byte

        // Wait for byte to arrive in RX FIFO, protected by a timeout
        uint32_t timeout = system_ticks + 100;
        while(!(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_RX_READY_OFFSET))) {
            if (system_ticks > timeout) {
                print_str("  [!] RX TIMEOUT\r\n");
                errors++;
                break;
            }
        }

        if (i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_NACK_OFFSET)) {
            print_str("  [!] NACK! Shrike refused read request.\r\n");
            errors++;
        } else {
            uint8_t rx_byte = i2c_master_rxtx_read(); // Pops from FIFO
            print_str("  [RX OK] Received: 0x"); print_hex8(rx_byte); print_str("\r\n");
        }
        delay_ms(100);
    }

    if (errors == 0) print_str("\r\n>> PASS (I2C Polling)\r\n");
    else print_str("\r\n>> FAIL (Check wiring or slave)\r\n");
}

// =============================================================================
// TEST 4: I2C INTERRUPTS (Event-Driven Read)
// =============================================================================
void test_i2c_interrupt(void) {
    print_str("\r\n[I2C IRQ] Flushing RX FIFO...\r\n");
    while(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_RX_READY_OFFSET)) {
        i2c_master_rxtx_read(); 
    }
    
    i2c_int_fired = 0;
    i2c_master_ev_pending_write(i2c_master_ev_pending_read());
    i2c_master_ev_enable_write(1 << CSR_I2C_MASTER_EV_ENABLE_RX_READY_OFFSET);

    print_str("[I2C IRQ] Triggering 3 reads. Waiting for CPU ISR to catch them...\r\n");
    i2c_master_addr_write(RP2040_I2C_ADDR);
    
    i2c_master_settings_write((0 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET) | 
                              (1 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET));

    uint32_t errors = 0;
    for (uint8_t i = 0; i < 3; i++) {
        i2c_master_rxtx_write(0x00);
        
        uint32_t timeout = system_ticks + 500;
        while (!i2c_int_fired && (system_ticks < timeout)) {}

        if (i2c_int_fired) {
            i2c_int_fired = 0; 
            print_str("  [ISR Caught] I2C RX Data: 0x"); print_hex8(i2c_rx_data_int); print_str("\r\n");
        } else {
            print_str("  [!] TIMEOUT: I2C Interrupt never fired!\r\n");
            errors++;
        }
        delay_ms(100);
    }

    i2c_master_ev_enable_write(0); 
    
    if (errors == 0) print_str("\r\n>> PASS (I2C Interrupts)\r\n");
    else print_str("\r\n>> FAIL (Check RP2040 slave firmware)\r\n");
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

    // I2C Hardware Enable
    i2c_master_active_write(1);

    // Add Timer0, UART1, and I2C to the CPU's global interrupt mask
    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << UART1_INTERRUPT) | (1 << I2C_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    while(1) {
        print_str("\r\n==================================\r\n");
        print_str(" HARDWARE VALIDATION MENU\r\n");
        print_str("==================================\r\n");
        print_str(" 1: UART1 Polling Test\r\n");
        print_str(" 2: UART1 Interrupt Test\r\n");
        print_str(" 3: I2C Polling Test (Requires RP2040)\r\n");
        print_str(" 4: I2C Interrupt Test (Requires RP2040)\r\n");
        print_str(" Select Test: ");
        
        char choice = read_char_blocking();
        print_char(choice);
        print_str("\r\n");

        if (choice == '1') {
            // test_uart1_polling();
        } else if (choice == '2') {
            // test_uart1_interrupt();
        } else if (choice == '3') {
            test_i2c_polling();
        } else if (choice == '4') {
            test_i2c_interrupt();
        } else {
            print_str(" Invalid selection.\r\n");
        }
    }
    return 0;
}
