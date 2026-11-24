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

void print_hex_32(uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        print_char(hex_chars[(val >> i) & 0x0F]);
    }
}

void system_init(void) {
    // --- Timer0 Setup (32-bit Down-Counter) ---
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); 
    timer0_load_write(0); 
    timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1 << CSR_TIMER0_EV_ENABLE_ZERO_OFFSET); 
    timer0_en_write(1);

    // --- Timer1 Setup (16-bit Up-Counter for PWM) ---
    timer1_ctrl_write(0); 
    
    // Set TOP to 50,000. At 50MHz system clock, this yields a 1 kHz PWM period.
    timer1_top_write(50000); 
    
    // Assign staggered duty cycles across the 4 channels
    timer1_cc_a_write(12500); // CH A: 25% Duty Cycle
    timer1_cc_b_write(25000); // CH B: 50% Duty Cycle
    timer1_cc_c_write(37500); // CH C: 75% Duty Cycle
    timer1_cc_d_write(45000); // CH D: 90% Duty Cycle
    
    timer1_ev_pending_write(timer1_ev_pending_read());
    timer1_ev_enable_write(1 << CSR_TIMER1_EV_ENABLE_OVERFLOW_OFFSET); 
    
    timer1_ctrl_write(1); 

    // Enable CPU interrupts 
    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << TIMER1_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    print_str("\r\n=== Timer0 and Timer1 Test ===\r\n");
    print_str("Timer0: Generating 1ms system ticks.\r\n");
    print_str("Timer1: Driving PWM on PMOD C (J1, L2, J2, G2).\r\n\r\n");

    while(1) {
        // Output from Timer0's 1-second flag
        if (flag_1sec) {
            flag_1sec = 0;
            
            // To read the live value of Timer0, write to update_value first
            timer0_update_value_write(1);
            uint32_t current_t0 = timer0_value_read();
            
            print_str("[TIMER0] 1 Second Elapsed. Live countdown value: 0x");
            print_hex_32(current_t0);
            print_str("\r\n");
        }

        // Output from Timer1's scaled overflow flag
        if (flag_timer1_update) {
            flag_timer1_update = 0;
            
            // Read the live counter of Timer1 (no update register needed per timer1.v)
            uint32_t current_t1 = timer1_tcnt_read();
            
            print_str("[TIMER1] 10,000 Overflows Reached. Total: 0x");
            print_hex_32(timer1_overflow_count);
            print_str(" | Live TCNT: 0x");
            print_hex_32(current_t1);
            print_str("\r\n");
        }
    }
    return 0;
}
