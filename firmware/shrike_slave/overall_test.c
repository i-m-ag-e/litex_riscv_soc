#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/irq.h"

// --- SHRIKE LITE PINS ---
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 16
#define UART_RX_PIN 17

#define SPI_ID spi0
#define SPI_SCK_PIN 22
#define SPI_CSN_PIN 21
#define SPI_RX_PIN 20 // MOSI
#define SPI_TX_PIN 23 // MISO

// --- UART HARDWARE INTERRUPT ---
// This fires instantly the microsecond the FPGA sends a UART ping
void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        // Echo back incremented by 1 (e.g., 'P' becomes 'Q')
        uart_putc(UART_ID, ch + 1);
    }
}

int main() {
    stdio_init_all();

    // ─── 1. INITIALIZE UART0 ───
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Attach the hardware interrupt
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    // ─── 2. INITIALIZE SPI0 SLAVE ───
    spi_init(SPI_ID, 1000 * 1000); // 1 MHz to match the FPGA
    spi_set_slave(SPI_ID, true);   // Set RP2040 as the Slave
    
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CSN_PIN, GPIO_FUNC_SPI);

    uint8_t dummy_sensor_data = 0xA5; 

    // ─── 3. MAIN LOOP ───
    while (1) {
        // The UART is handled by the interrupt in the background.
        // We dedicate the main CPU loop purely to checking SPI traffic.
        
        if (spi_is_readable(SPI_ID)) {
            uint8_t incoming_data;
            // Read whatever the FPGA just pushed to us
            spi_read_blocking(SPI_ID, 0, &incoming_data, 1);
            
            // Immediately load our dummy data into the TX buffer 
            // so it is waiting for the FPGA's next clock cycle
            spi_write_blocking(SPI_ID, &dummy_sensor_data, 1);
        }
        tight_loop_contents(); // Standard SDK CPU yield
    }
}
