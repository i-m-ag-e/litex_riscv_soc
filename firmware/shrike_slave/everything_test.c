#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/irq.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 16
#define UART_RX_PIN 17

#define SPI_ID spi0
#define SPI_SCK_PIN 22
#define SPI_CSN_PIN 21
#define SPI_RX_PIN 20 // MOSI 
#define SPI_TX_PIN 23 // MISO 

// ─── UART BACKGROUND ECHO ───
// Instantly catches the FPGA's routed keystrokes and shoots them back
void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        uart_putc(UART_ID, ch + 1); // Echo back incremented
    }
}

int main() {
    stdio_init_all();

    // ─── 1. SETUP UART ───
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    // ─── 2. SETUP SPI SLAVE ───
    spi_init(SPI_ID, 1000 * 1000); 
    spi_set_slave(SPI_ID, true);   
    
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CSN_PIN, GPIO_FUNC_SPI);

    // ─── 3. RAW HARDWARE LOOP ───
    while (1) {
        // Perpetually lock 0xA5 into the transmit chamber so the 
        // FPGA's SPI_DONE interrupt has a valid payload to read.
        if (spi_is_writable(SPI_ID)) {
            spi_get_hw(SPI_ID)->dr = 0xA5; 
        }

        // Dump incoming async triggers so we don't overflow
        if (spi_is_readable(SPI_ID)) {
            volatile uint32_t dump = spi_get_hw(SPI_ID)->dr; 
        }
        
        tight_loop_contents(); 
    }
}
