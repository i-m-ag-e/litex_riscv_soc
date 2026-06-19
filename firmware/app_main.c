#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "irq.h"

// ─── 1. GLOBALS ───
volatile uint32_t system_ticks = 0;
volatile uint8_t  flag_100ms   = 0;

volatile uint32_t timer1_periods = 0; // Incremented by Timer1 overflow interrupt

#define RX_BUF_SIZE 64
volatile uint8_t  rx_buffer[RX_BUF_SIZE];
volatile uint16_t rx_head = 0; 
volatile uint16_t rx_tail = 0; 

// ─── 2. THE FAST INTERRUPT HANDLER ───
void isr(void) {
    // A. Timer0 (1ms system tick)
    if (timer0_ev_pending_read() & 1) {
        system_ticks++;
        if (system_ticks % 100 == 0) {
            flag_100ms = 1; 
        }
        timer0_ev_pending_write(1); 
    }

    // B. UART RX
    if (uart_ev_pending_read() & (1 << CSR_UART_EV_PENDING_RX_OFFSET)) {
        while (!uart_rxempty_read()) {
            rx_buffer[rx_head] = uart_rxtx_read();
            rx_head = (rx_head + 1) % RX_BUF_SIZE;
        }
        uart_ev_pending_write(1 << CSR_UART_EV_PENDING_RX_OFFSET);
    }

    // C. Timer1 PWM Overflow (Fires when PWM counter hits TOP)
    if (timer1_ev_pending_read() & (1 << CSR_TIMER1_EV_PENDING_OVERFLOW_OFFSET)) {
        timer1_periods++;
        timer1_ev_pending_write(1 << CSR_TIMER1_EV_PENDING_OVERFLOW_OFFSET);
    }
}

// ─── 3. UART TRANSMIT UTILITY ───
void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

// ─── 4. MAIN APPLICATION ───
int main(void) {
    // 1. Setup Yellow LEDs (PORTB)
    portb_out_write(0b10101010);

    // 2. Setup Tristate Pins (PORTD) as Outputs
    // Write 0x0F to Output Enable to make ONLY the first 4 pins outputs

    return 0;
}
