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

void system_init(void) {
    // Timer0 Setup (1ms ticks)
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); 
    timer0_load_write(0); 
    timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1 << CSR_TIMER0_EV_ENABLE_ZERO_OFFSET); 
    timer0_en_write(1);

    // Timer1 Setup (PWM)
    timer1_ctrl_write(0); 
    timer1_top_write(50000); // 1 kHz PWM period at 50MHz
    
    // Start all channels at 0
    timer1_cc_a_write(0); 
    timer1_cc_b_write(0); 
    timer1_cc_c_write(0); 
    timer1_cc_d_write(0); 
    
    timer1_ev_pending_write(timer1_ev_pending_read());
    timer1_ev_enable_write(1 << CSR_TIMER1_EV_ENABLE_OVERFLOW_OFFSET); 
    timer1_ctrl_write(1); 

    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << TIMER1_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    
    print_str("\r\n=== PWM Breathing LED Test ===\r\n");
    print_str("All PMOD C channels will now pulse.\r\n");

    uint32_t last_tick = 0;
    int32_t  pwm_val   = 0;
    int32_t  pwm_step  = 500; // Adjust for speed of breathing

    while(1) {
        // Update the PWM duty cycle every 10 milliseconds
        if ((system_ticks - last_tick) >= 10) {
            last_tick = system_ticks;

            pwm_val += pwm_step;

            // Reverse direction at the boundaries
            if (pwm_val >= 50000) {
                pwm_val = 50000;
                pwm_step = -pwm_step;
            } else if (pwm_val <= 0) {
                pwm_val = 0;
                pwm_step = -pwm_step;
            }

            // Apply the new value to all channels
            timer1_cc_a_write(pwm_val);
            timer1_cc_b_write(pwm_val);
            timer1_cc_c_write(pwm_val);
            timer1_cc_d_write(pwm_val);
        }

        // Keep printing the 1-second heartbeat to ensure the system hasn't frozen
        if (flag_1sec) {
            flag_1sec = 0;
            print_str("System active. Current PWM Duty: ");
            
            // Print the percentage roughly (pwm_val / 500)
            uint32_t percent = pwm_val / 500;
            
            // Hacky print for numbers without importing printf
            char buf[4];
            buf[0] = '0' + (percent / 100);
            buf[1] = '0' + ((percent / 10) % 10);
            buf[2] = '0' + (percent % 10);
            buf[3] = '\0';
            
            if (buf[0] == '0') buf[0] = ' ';
            if (buf[0] == ' ' && buf[1] == '0') buf[1] = ' ';
            
            print_str(buf);
            print_str("%\r\n");
        }
    }
    return 0;
}
