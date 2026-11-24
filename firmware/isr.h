#ifndef ISR_H
#define ISR_H

#include <stdint.h>

extern volatile uint32_t system_ticks;
extern volatile uint8_t  flag_1sec;

// Timer1 tracking
extern volatile uint32_t timer1_overflow_count;
extern volatile uint8_t  flag_timer1_update;

extern volatile uint8_t  spi_rx_data_int;
extern volatile uint8_t  i2c_rx_data_int;
extern volatile uint8_t  spi_int_fired;
extern volatile uint8_t  i2c_int_fired;
extern volatile uint8_t  uart1_rx_byte;
extern volatile uint8_t  uart1_rx_flag;

void delay_ms(uint32_t ms);
void isr(void);

#endif // ISR_H
