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
    
    portb_out_write(0xAA);
    delay_ms(1000);
    portb_out_write(0x55);
    delay_ms(1000);
    
    uint8_t counter = 0;
    while (1) {
        int dir = portc_in_read() & 0x01 ? -1 : 1;
        portb_out_write(counter = counter + dir);
        delay_ms(500);
    }

    return 0;
}
