#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MOSI 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MISO 19

int main() {
    stdio_init_all();
    spi_init(SPI_PORT, 1000 * 1000);
    spi_set_slave(SPI_PORT, true);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    sleep_ms(2000);
    printf("\n--- Shrike SPI Full-Duplex Slave Ready ---\n");
    
    uint8_t rp2040_tx_counter = 0xC0; // Starts at C0

    while (1) {
        uint8_t rx_data = 0;
        uint8_t tx_data = rp2040_tx_counter;
        
        // This function simultaneously sends tx_data on MISO and receives rx_data from MOSI
        spi_write_read_blocking(SPI_PORT, &tx_data, &rx_data, 1);
        
        printf("[SPI] Received: 0x%02X | Sent: 0x%02X\n", rx_data, tx_data);
        rp2040_tx_counter++; 
    }
    return 0;
}
