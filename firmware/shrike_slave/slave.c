#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"

// Define I2C hardware and pins (Must be a valid pair, e.g., 4/5 for I2C0)
#define I2C_PORT i2c0
#define I2C_SLAVE_ADDR 0x42
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

// ─── INTERRUPT HANDLER ───
static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
        case I2C_SLAVE_RECEIVE: { 
            // Phase 1 & 2: FPGA Master is writing to us
            uint8_t rx_data = i2c_read_byte_raw(i2c);
            printf("[I2C RX] Received from FPGA: 0x%02X\n", rx_data);
            break;
        }
        case I2C_SLAVE_REQUEST: { 
            // Phase 3: FPGA Master is reading from us
            uint8_t tx_data = 0xAA; // Dummy byte
            i2c_write_byte_raw(i2c, tx_data);
            printf("[I2C TX] Sent to FPGA: 0x%02X\n", tx_data);
            break;
        }
        case I2C_SLAVE_FINISH: 
            // Master sent STOP condition
            break;
        default:
            break;
    }
}

int main() {
    // Initialize standard I/O (USB UART)
    stdio_init_all();

    // Initialize I2C pins
    gpio_init(I2C_SDA_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);

    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SCL_PIN);

    // Initialize I2C hardware at 100 kHz
    i2c_init(I2C_PORT, 100 * 1000);

    // Initialize as Slave and register the interrupt handler
    i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDR, &i2c_slave_handler);

    // Wait briefly for USB serial to connect if monitoring immediately
    sleep_ms(2000); 

    printf("\n====================================\n");
    printf(" Shrike RP2040 I2C Slave Active\n");
    printf(" Listening on Address: 0x%02X\n", I2C_SLAVE_ADDR);
    printf("====================================\n");

    while (1) {
        tight_loop_contents();
    }

    return 0;
}
