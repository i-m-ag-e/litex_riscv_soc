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
    // Timer0 Setup (1ms ticks)
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); 
    timer0_load_write(0); 
    timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1 << CSR_TIMER0_EV_ENABLE_ZERO_OFFSET); 
    timer0_en_write(1);

    // Setup Port B as output (LEDs) and Port C as input (Switches)
    portb_oe_write(0xFF); 
    portc_oe_write(0x00); 

    irq_setmask(1 << TIMER0_INTERRUPT);
    irq_setie(1);
}

int main(void) {
    system_init();
    
    print_str("\r\n========================================\r\n");
    print_str("=== System Boot / Watchdog Test Init ===\r\n");
    print_str("========================================\r\n");
    print_str("Watchdog timeout set to 5 seconds.\r\n");
    print_str("SW0 DOWN: Feed the watchdog (prevents reset).\r\n");
    print_str("SW0 UP:   Starve the watchdog (causes reset).\r\n\r\n");

    // Configure watchdog for 5 seconds[cite: 5]
    watchdog_load_write(CONFIG_CLOCK_FREQUENCY * 5); 
    
    // Enable the watchdog hardware[cite: 5]
    watchdog_ctrl_write(1); 

    uint8_t led_counter = 0;

    while(1) {
        if (flag_1sec) {
            flag_1sec = 0;
            
            // Output current counter value to the physical LEDs[cite: 5]
            portb_out_write(led_counter);
            led_counter++;
            
            // Read Switch 0 state[cite: 5]
            uint8_t sw0 = portc_in_read() & 0x01;
            
            print_str("[WDOG] Counter: 0x");
            print_hex_32(watchdog_counter_read());
            
            if (sw0 == 0) {
                print_str(" | Feeding...\r\n");
                // Write any value to the feed register to reload the timer[cite: 5, 7]
                watchdog_feed_write(1);
            } else {
                print_str(" | Starving...\r\n");
            }
        }
    }
    return 0;
}
