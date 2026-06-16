#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

// Switched to uart0 for GP16/GP17
#define UART_ID uart0 
#define BAUD_RATE 115200

#define UART_TX_PIN 16 // RP2040 TX -> Connect to FPGA RX (A16)
#define UART_RX_PIN 17 // RP2040 RX -> Connect to FPGA TX (A14)

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("\nShrike RP2040 UART Active\n");

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    while (1) {
        if (uart_is_readable(UART_ID)) {
            uint8_t ch = uart_getc(UART_ID);
            
            printf("[RP2040 RX] Received: %c\n", ch);
            
            uart_puts(UART_ID, "   [ACK '");
            uart_putc(UART_ID, ch);
            uart_puts(UART_ID, "']\r\n");
        }
    }

    return 0;
}
