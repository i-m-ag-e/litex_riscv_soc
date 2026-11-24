// Weak so an app can define its own isr()/system_ticks/delay_ms instead of these.
#include "isr.h"
#include <generated/csr.h>

__attribute__((weak)) volatile uint32_t system_ticks = 0;
volatile uint8_t  flag_1sec    = 0;
volatile uint8_t  spi_rx_data_int = 0;
volatile uint8_t  i2c_rx_data_int = 0;
volatile uint8_t  spi_int_fired   = 0;
volatile uint8_t  i2c_int_fired   = 0;

volatile uint8_t  uart1_rx_byte = 0;
volatile uint8_t  uart1_rx_flag = 0;

volatile uint32_t timer1_overflow_count = 0;
volatile uint8_t  flag_timer1_update    = 0;

__attribute__((weak)) void delay_ms(uint32_t ms) {
    uint32_t start_time = system_ticks;
    while ((system_ticks - start_time) < ms) {}
}

__attribute__((weak)) void isr(void) {
    // 1. Timer0 Interrupt
    if (timer0_ev_pending_read() & (1 << CSR_TIMER0_EV_PENDING_ZERO_OFFSET)) {
        system_ticks++;
        if (system_ticks % 1000 == 0) { flag_1sec = 1; }
        timer0_ev_pending_write(1 << CSR_TIMER0_EV_PENDING_ZERO_OFFSET); 
    }

    // 2. Timer1 Interrupt (PWM Overflow)
    if ((timer1_ev_pending_read() & (1 << CSR_TIMER1_EV_PENDING_OVERFLOW_OFFSET)) &&
        (timer1_ev_enable_read()  & (1 << CSR_TIMER1_EV_ENABLE_OVERFLOW_OFFSET))) {
        
        timer1_overflow_count++;
        // Flag the main loop every 10,000 overflows to print a status update
        if (timer1_overflow_count % 10000 == 0) {
            flag_timer1_update = 1;
        }
        timer1_ev_pending_write(1 << CSR_TIMER1_EV_PENDING_OVERFLOW_OFFSET);
    }

    // 2. SPI Interrupt (Only service if BOTH pending AND enabled)
    if ((spi_master_ev_pending_read() & (1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET)) &&
            (spi_master_ev_enable_read()  & (1 << CSR_SPI_MASTER_EV_ENABLE_RX_READY_OFFSET))) {

        spi_rx_data_int = spi_master_rxtx_read();
        spi_master_cs_write(0);
        spi_int_fired = 1;
        spi_master_ev_pending_write(1 << CSR_SPI_MASTER_EV_PENDING_RX_READY_OFFSET);
    }

    // 3. I2C Interrupt (Reverted to Basys3 behavior)
    if (i2c_master_ev_pending_read() & (1 << CSR_I2C_MASTER_EV_PENDING_RX_READY_OFFSET)) {
        i2c_rx_data_int = i2c_master_rxtx_read();
        i2c_int_fired = 1;
        i2c_master_ev_pending_write(1 << CSR_I2C_MASTER_EV_PENDING_RX_READY_OFFSET);
    }

    // 4. UART1 Interrupt (Only service if BOTH pending AND enabled)
    if ((uart1_ev_pending_read() & (1 << CSR_UART1_EV_PENDING_RX_OFFSET)) &&
            (uart1_ev_enable_read()  & (1 << CSR_UART1_EV_ENABLE_RX_OFFSET))) {

        while (!uart1_rxempty_read()) {
            uart1_rx_byte = uart1_rxtx_read();
            uart1_rx_flag = 1; 
        }
        uart1_ev_pending_write(1 << CSR_UART1_EV_PENDING_RX_OFFSET);
    }
}
