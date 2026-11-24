#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"
#include "isr.h"

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

void system_init(void) {
    // Timer Init
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); timer0_load_write(0); timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(CSR_TIMER0_EV_ENABLE_ZERO_SIZE); 
    timer0_en_write(1);

    // Setup GPIO: Port B (LEDs) as output, Port C (Switches) as input
    portb_oe_write(0xFF); 
    portc_oe_write(0x00); 

    // SPI Base Configuration: 8-bit length, 1-bit width, mask 1
    spi_master_phyconfig_write((8 << CSR_SPI_MASTER_PHYCONFIG_LEN_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_WIDTH_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_MASK_OFFSET));

    // Enable CPU interrupts (SPI IRQ will be toggled dynamically in main)
    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << SPI_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    print_str("\r\n=== Unified SPI Test (Polling & IRQ) ===\r\n");
    print_str("Use Switch 0 (Port C, Pin V17) to toggle modes:\r\n");
    print_str("  SW0 DOWN = Polling Mode\r\n");
    print_str("  SW0 UP   = Interrupt Mode\r\n\r\n");

    uint8_t test_val = 0x41; 
    uint8_t mode_prev = 0xFF;

    while(1) {
        // Read the state of Switch 0[cite: 5]
        uint8_t current_mode = portc_in_read() & 0x01; 
        
        // Handle physical switch toggle
        if (current_mode != mode_prev) {
            if (current_mode) {
                print_str("\r\n--- Switched to INTERRUPT Mode ---\r\n");
                // Clear pending and enable IRQ[cite: 5]
                spi_master_ev_pending_write(spi_master_ev_pending_read());
                spi_master_ev_enable_write(1 << CSR_SPI_MASTER_EV_ENABLE_RX_READY_OFFSET);
            } else {
                print_str("\r\n--- Switched to POLLING Mode ---\r\n");
                // Disable IRQ[cite: 5]
                spi_master_ev_enable_write(0); 
            }
            mode_prev = current_mode;
        }

        // Transmit logic (Fires every 1 second)
        if (flag_1sec) {
            flag_1sec = 0;
            print_str("[MAIN] TX: "); print_char((char)test_val); print_str(" -> ");
            
            // Assert CS and initiate transfer[cite: 5]
            spi_master_cs_write(1);
            spi_master_rxtx_write(test_val);
            
            if (current_mode == 0) {
                // --- POLLING MODE LOGIC ---
                // Block until hardware sets the RX_READY flag[cite: 5]
                while(!(spi_master_ev_pending_read() & (1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET)));
                
                // Read data, de-assert CS, and clear the event flag manually[cite: 5]
                uint8_t rx_val = spi_master_rxtx_read();
                spi_master_cs_write(0);
                spi_master_ev_pending_write(1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET); 
                
                print_str("[POLL] RX: "); print_char((char)rx_val); print_str(" ("); print_hex32(rx_val); print_str(")"); print_str("\r\n");
            }
            
            test_val++;
            if (test_val > 0x5A) test_val = 0x41;
        }

        // --- INTERRUPT MODE LOGIC ---
        // Catch the flag set by isr.c
        if (spi_int_fired) {
            spi_int_fired = 0;
            print_str("[ISR]  RX: "); print_char((char)spi_rx_data_int); print_str(" ("); print_hex32(spi_rx_data_int); print_str(")"); print_str("\r\n");
        }
    }
    return 0;
}
