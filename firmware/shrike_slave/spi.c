#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ⚠️ UPDATE THESE PINS to match the Shrike FPGA bridge traces!
#define SPI_PORT spi0
// ─── SHRIKE FPGA BRIDGE PINS (SPI0) ───
#define PIN_MOSI 16 // RP2040 RX 
#define PIN_CS   17 // RP2040 CSn
#define PIN_SCK  18 // RP2040 SCK
#define PIN_MISO 19 // RP2040 TX

int main() {
    stdio_init_all();

    spi_init(SPI_PORT, 1000 * 1000); // 1 MHz
    spi_set_slave(SPI_PORT, true);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    sleep_ms(2000);
    printf("\nShrike RP2040 SPI Bidirectional Slave Active\n");

    uint8_t rp2040_tx_counter = 0xAA;

    while (1) {
        uint8_t rx_data = 0;
        uint8_t tx_data = rp2040_tx_counter;

        // Blocks until the FPGA initiates a transfer
        spi_write_read_blocking(SPI_PORT, &tx_data, &rx_data, 1);

        if (rx_data != 0x00) {
            // FPGA is in SEND mode (Switch DOWN)
            printf("[RP2040 RX] FPGA says: 0x%02X\n", rx_data);
        } else {
            // FPGA is in RECEIVE mode (Switch UP)
            printf("[RP2040 TX] Sent to FPGA: 0x%02X\n", tx_data);
            rp2040_tx_counter++; // Queue the next number for the next read
        }
    }

    return 0;
}
