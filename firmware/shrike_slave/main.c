#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"

#define I2C_PORT i2c0
#define I2C_SLAVE_ADDR 0x42
#define I2C_SDA_PIN 16
#define I2C_SCL_PIN 17

static uint8_t rp2040_tx_counter = 0xD0; // Starts at D0

static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
        case I2C_SLAVE_RECEIVE: {
            uint8_t rx_data = i2c_read_byte_raw(i2c);
            printf("[I2C RX] Received from FPGA: 0x%02X\n", rx_data);
            break;
        }
        case I2C_SLAVE_REQUEST: {
            rp2040_tx_counter++;
            i2c_write_byte_raw(i2c, rp2040_tx_counter);
            printf("[I2C TX] Sent to FPGA: 0x%02X\n", rp2040_tx_counter);
            break;
        }
        case I2C_SLAVE_FINISH:
            break;
        default:
            break;
    }
}

int main() {
    stdio_init_all();
    gpio_init(I2C_SDA_PIN); gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C); gpio_pull_up(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN); gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C); gpio_pull_up(I2C_SCL_PIN);
    
    i2c_init(I2C_PORT, 100 * 1000);
    i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDR, &i2c_slave_handler);
    
    sleep_ms(2000);
    printf("\n--- Shrike I2C Half-Duplex Slave Ready ---\n");
    printf("Listening on Address: 0x%02X\n", I2C_SLAVE_ADDR);
    
    while (1) {
        tight_loop_contents();
    }
    return 0;
}
