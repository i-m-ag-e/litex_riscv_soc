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
    portb_out_write(0x00);

    // 2. Setup Tristate Pins (PORTD) as Outputs
    // Write 0x0F to Output Enable to make ONLY the first 4 pins outputs
    portd_oe_write(0x0F); 
    portd_out_write(0x00);

    // 3. Configure Timer0
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0);                 
    timer0_load_write(0);               
    timer0_reload_write(ticks_per_ms);  
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1);
    timer0_en_write(1); 

    // 4. Configure UART RX Interrupt
    uart_ev_pending_write(uart_ev_pending_read());
    uart_ev_enable_write(1 << CSR_UART_EV_ENABLE_RX_OFFSET);

    // 5. Configure Timer1 (Hardware PWM AND Overflow Interrupt)
    timer1_ctrl_write(0);                 
    timer1_top_write(20000);              
    timer1_cc_a_write(0); // Pin A15                 
    timer1_cc_b_write(0); // Pin A17 
    timer1_ev_pending_write(timer1_ev_pending_read()); // Clear stale
    timer1_ev_enable_write(1 << CSR_TIMER1_EV_ENABLE_OVERFLOW_OFFSET); // ENABLE IRQ               
    timer1_ctrl_write(1);                 

    // 6. Unmask ALL CPU Interrupts
    irq_setmask(
        (1 << TIMER0_INTERRUPT) | 
        (1 << UART_INTERRUPT)   | 
        (1 << TIMER1_INTERRUPT)
    );
    irq_setie(1);

    // Logic Variables
    uint8_t  led_counter = 0;
    
    uint16_t pwm_duty = 0;
    int8_t   pwm_direction = 1;
    
    uint32_t last_timer1_periods = 0;
    uint8_t  portd_chaser_idx = 0;

    // ─── SUPER LOOP ───
    while(1) {
        
        // Task 1: Check Timer0 (100ms intervals)
        if (flag_100ms) {
            flag_100ms = 0; 
            
            // Increment the yellow FPGA LEDs
            led_counter++;
            portb_out_write(led_counter);
            
            // Animate the hardware PWM breathing on A15 and A17
            if (pwm_direction == 1) {
                pwm_duty += 500;
                if (pwm_duty >= 20000) pwm_direction = -1;
            } else {
                pwm_duty -= 500;
                if (pwm_duty == 0) pwm_direction = 1;
            }
            timer1_cc_a_write(pwm_duty);
            timer1_cc_b_write(20000 - pwm_duty); 
        }

        // Task 2: Check Timer1 (Independent timing for the PORTD Chaser)
        // At 50MHz with a TOP of 20,000, the overflow interrupt fires 2500 times a second.
        // We wait for 625 periods to pass (exactly 250 milliseconds).
        if ((timer1_periods - last_timer1_periods) >= 625) {
            last_timer1_periods = timer1_periods;
            
            // Output a 1 to exactly one of the 4 LEDs
            portd_out_write(1 << portd_chaser_idx);
            
            // Move to the next LED (0, 1, 2, 3 -> wrap to 0)
            portd_chaser_idx = (portd_chaser_idx + 1) % 4;
        }

        // Task 3: Check the UART Ring Buffer
        if (rx_tail != rx_head) {
            uint8_t incoming_char = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
            
            if (incoming_char >= 'a' && incoming_char <= 'z') {
                print_char(incoming_char - 32); 
            } else {
                print_char(incoming_char);
            }
        }

        watchdog_feed_write(1);
    }

    return 0;
}
