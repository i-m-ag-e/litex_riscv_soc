#include <stdint.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

volatile uint32_t uptime_ms;

void board_init(void) {
    uptime_ms = 0;
    portb_out_write(0x00);

    // Start 1ms hardware tick timer
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0);                 
    timer0_load_write(0);               
    timer0_reload_write(ticks_per_ms);  
    timer0_en_write(1);                 
    timer0_ev_pending_write(timer0_ev_pending_read());
}

void board_tick(void) {
    if (timer0_ev_pending_read() & 1) {
        uptime_ms++;
        timer0_ev_pending_write(1); 
    }
}

// ─── UART & Printing Drivers ───
void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

void print_str(const char *str) {
    while(*str) print_char(*str++);
}

void print_int16(int16_t val) {
    if (val < 0) {
        print_char('-');
        val = -val;
    }
    if (val == 0) { print_char('0'); return; }
    int divisor = 10000;
    int started = 0;
    while (divisor > 0) {
        int digit = val / divisor;
        if (digit > 0 || started) {
            print_char('0' + digit);
            started = 1;
        }
        val %= divisor;
        divisor /= 10;
    }
}

void delay_ms(uint32_t ms) {
    uint32_t start_time = uptime_ms;
    while ((uptime_ms - start_time) < ms) {
        board_tick(); 
    }
}

// ─── Timer1 Test Suite ───

// TEST 1: Core Logic - Wishbone Bus Integrity
// Verifies that the CPU can successfully read and write to the custom CSRs.
void test1_wishbone_csr(void) {
    print_str("Test 1: Wishbone CSR R/W... ");
    timer1_ctrl_write(0);
    
    // Write distinct test patterns
    timer1_top_write(0xABCD);
    timer1_cc_a_write(0x1234);
    timer1_cc_b_write(0x5678);
    
    // Read back and verify
    if (timer1_top_read() != 0xABCD || timer1_cc_a_read() != 0x1234 || timer1_cc_b_read() != 0x5678) {
        print_str("[FAIL] Data mismatch!\r\n");
    } else {
        print_str("[PASS]\r\n");
    }
}

// TEST 2: Core Logic - Counter Rollover & TOP verification
// Proves the counter actually stops at TOP and resets to 0.
void test2_counter_rollover(void) {
    print_str("Test 2: Counter Rollover... ");
    timer1_ctrl_write(0);
    
    uint16_t test_top = 5000;
    timer1_top_write(test_top);
    timer1_ctrl_write(1);
    
    uint16_t max_observed = 0;
    uint8_t failed = 0;
    
    // Rapidly sample the counter 50,000 times
    for(volatile int i = 0; i < 50000; i++) {
        uint16_t current = timer1_tcnt_read();
        if (current > max_observed) {
            max_observed = current;
        }
        if (current > test_top) {
            failed = 1;
            break;
        }
    }
    
    timer1_ctrl_write(0);
    
    if (failed) {
        print_str("[FAIL] Counter exceeded TOP!\r\n");
    } else {
        print_str("[PASS] Max reached: ");
        print_int16(max_observed);
        print_str("\r\n");
    }
}

// TEST 3: Hardware PWM - Static Multi-Channel
// Sets 4 distinct duty cycles. Hook LEDs or an oscilloscope to the PMOD pins.
void test3_static_pwm(void) {
    print_str("Test 3: Static 4-Channel PWM initialized.\r\n");
    print_str(" -> Check PMOD pins. LEDs should step from dim to bright.\r\n");
    
    timer1_ctrl_write(0);
    timer1_top_write(40000); // 40k ticks per period
    
    timer1_cc_a_write(1000);  // ~2% (Very Dim)
    timer1_cc_b_write(10000); // 25% (Dim)
    timer1_cc_c_write(20000); // 50% (Medium)
    timer1_cc_d_write(38000); // 95% (Bright)
    
    timer1_ctrl_write(1);
}

// ─── Core User Routine ───

// State variables for Test 4
uint32_t last_pwm_update_ms;
int16_t current_duty = 0;
int16_t duty_step = 500;
uint16_t pwm_top = 20000;

void setup(void) {
    print_str("\r\n===================================\r\n");
    print_str(" ASIC Timer1 Validation Suite\r\n");
    print_str("===================================\r\n");

    test1_wishbone_csr();
    test2_counter_rollover();
    test3_static_pwm();
    
    delay_ms(3000); // Leave static PWM on for 3 seconds to verify visually
    
    print_str("\r\nTest 4: Dynamic Breathing LED started on Channel A.\r\n");
    
    // Prepare Test 4 (Dynamic Sweep)
    timer1_ctrl_write(0);
    timer1_top_write(pwm_top);
    timer1_cc_a_write(0);
    // Turn off B, C, D so we only watch A
    timer1_cc_b_write(0); 
    timer1_cc_c_write(0);
    timer1_cc_d_write(0);
    timer1_ctrl_write(1);
    
    last_pwm_update_ms = uptime_ms;
}

// TEST 4: Hardware PWM - Dynamic Sweeping (Breathing LED)
// Runs continuously in the super-loop. Tests the shadow register glitch-free updates.
void loop(void) {
    // Update the duty cycle every 15 milliseconds
    if ((uptime_ms - last_pwm_update_ms) >= 15) {
        last_pwm_update_ms = uptime_ms;

        // Write the new duty cycle (the Verilog shadow register will latch it on the next rollover)
        timer1_cc_a_write(current_duty);
        
        // Calculate next step
        current_duty += duty_step;
        if (current_duty >= pwm_top) {
            current_duty = pwm_top;
            duty_step = -duty_step; // Reverse direction (fade out)
        } else if (current_duty <= 0) {
            current_duty = 0;
            duty_step = -duty_step; // Reverse direction (fade in)
        }
        
        // If you were using the watchdog, you would pet it right here:
        // watchdog_feed_write(1); 
    }
}

int main(void) {
    board_init();
    setup();
    
    while(1) {
        board_tick(); 
        loop();       
    }
    
    return 0;
}
