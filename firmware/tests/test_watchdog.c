#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"

// ─── 1. GLOBALS ───
volatile uint32_t system_ticks = 0;
volatile uint8_t  flag_100ms   = 0;

// Watchdog 3-second math
#define WDT_3_SECONDS (CONFIG_CLOCK_FREQUENCY * 3)

// ─── 2. THE FAST INTERRUPT HANDLER ───
// Kept intact so the compiler doesn't throw a fit.
// Stripped down to ONLY Timer0, which drives our LED counter timing.
void isr(void) {
    // Timer0 (1ms system tick)
    if (timer0_ev_pending_read() & 1) {
        system_ticks++;
        if (system_ticks % 100 == 0) {
            flag_100ms = 1; 
        }
        timer0_ev_pending_write(1); 
    }
}

// ─── 3. SYSTEM INITIALIZATION ───
// Setup is strictly limited to the LEDs and the Timer0 tick.
void system_init(void) {
    // 1. Setup Yellow LEDs (PORTB)
    portb_out_write(0x00);

    // 2. Setup Timer0 (1ms tick interval)
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0);                 
    timer0_load_write(0);               
    timer0_reload_write(ticks_per_ms);  
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1);
    timer0_en_write(1); 

    // 3. Unmask CPU Interrupt ONLY for Timer0
    irq_setmask(1 << TIMER0_INTERRUPT);
    irq_setie(1);
}

// ─── 4. MAIN APPLICATION ───
int main(void) {
    
    // Initialize hardware needed for the test
    system_init();

    // Read Switch 0 (PORTC) upon boot to determine test mode
    uint32_t switches = portc_in_read();
    uint8_t starve_mode = (switches & 0x02); 

    // Enable the Watchdog Timer for exactly 3 seconds
    watchdog_load_write(WDT_3_SECONDS);
    watchdog_ctrl_write(1);

    // Logic Variables
    uint8_t led_counter  = 0;
    uint8_t tick_divider = 0;

    // ─── SUPER LOOP ───
    while(1) {
        
        // Task 1: Non-blocking LED Counter (Using Timer0's 100ms flag)
        if (flag_100ms) {
            flag_100ms = 0; 
            tick_divider++;
            
            // Update the LEDs every 300ms so the count is visible
            if (tick_divider >= 2) {
                tick_divider = 0;
                portb_out_write(led_counter);
                led_counter++;
            }
        }

        // Task 2: The Watchdog Test
        if (!starve_mode) {
            // SW0 is LOW: Feed the dog continuously. System counts forever.
            watchdog_feed_write(1);
        }
        // If SW0 is HIGH (starve_mode == 1), we do not feed it. 
        // The hardware resets the board in exactly 3 seconds.
    }

    return 0;
}
