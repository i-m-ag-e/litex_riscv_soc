#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "irq.h"
#include "isr.h"

void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

void print_str(const char *str) {
    while(*str) print_char(*str++);
}

void system_init(void) {
    // Timer Init
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); timer0_load_write(0); timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(CSR_TIMER0_EV_ENABLE_ZERO_SIZE); 
    timer0_en_write(1);

    portb_oe_write(0xFF); 
    portc_oe_write(0x00); 
    i2c_master_active_write(1);

    spi_master_phyconfig_write((8 << CSR_SPI_MASTER_PHYCONFIG_LEN_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_WIDTH_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_MASK_OFFSET));

    // --- NEW: Clear and Enable UART1 RX Interrupts ---
    uart1_ev_pending_write(uart1_ev_pending_read());
    uart1_ev_enable_write(1 << CSR_UART1_EV_ENABLE_RX_OFFSET);

    // Add UART1_INTERRUPT to the CPU's interrupt mask
    irq_setmask((1 << TIMER0_INTERRUPT) | 
                (1 << SPI_INTERRUPT) | 
                (1 << I2C_INTERRUPT) | 
                (1 << UART1_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    print_str("\r\n=== UART1 Interrupt Loopback Test ===\r\n");
    print_str("Ensure PMOD JB Pins B15 (TX) and B16 (RX) are connected.\r\n");

    char test_char = 'A';

    while(1) {
        // 1. Send a character every 1 second
        if (flag_1sec) {
            flag_1sec = 0;
            
            print_str("[MAIN] Sending out UART1: "); 
            print_char(test_char); 
            print_str("\r\n");
            
            uart1_rxtx_write(test_char);
            
            test_char++;
            if (test_char > 'Z') test_char = 'A';
        }

        // 2. Immediately print whenever the hardware interrupt catches a bounced character
        if (uart1_rx_flag) {
            uart1_rx_flag = 0;
            print_str("[ISR]  Caught on UART1 RX: "); 
            print_char(uart1_rx_byte); 
            print_str("\r\n--------------------------------\r\n");
        }
    }
    return 0;
}
