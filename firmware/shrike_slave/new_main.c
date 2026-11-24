#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"

#define I2C_PORT i2c0
#define I2C_SLAVE_ADDRESS 0x42
#define I2C_SDA_PIN 16
#define I2C_SCL_PIN 17
#define BUF_SIZE 32

volatile uint8_t tx_counter = 0x11;

volatile uint8_t rx_buf[BUF_SIZE];
volatile uint8_t rx_head = 0, rx_tail = 0;

volatile uint8_t tx_buf[BUF_SIZE];
volatile uint8_t tx_head = 0, tx_tail = 0;

// Interrupt handles the raw speed of I2C
static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
        case I2C_SLAVE_RECEIVE:
            rx_buf[rx_head] = i2c_read_byte_raw(i2c);
            rx_head = (rx_head + 1) % BUF_SIZE;
            break;
        case I2C_SLAVE_REQUEST:
            i2c_write_byte_raw(i2c, tx_counter);
            tx_buf[tx_head] = tx_counter;
            tx_head = (tx_head + 1) % BUF_SIZE;
            tx_counter++; 
            break;
        case I2C_SLAVE_FINISH: 
            break;
    }
}

int main() {
    stdio_init_all();
    
    gpio_init(I2C_SDA_PIN); gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C); gpio_pull_up(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN); gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C); gpio_pull_up(I2C_SCL_PIN);

    i2c_init(I2C_PORT, 100 * 1000);
    i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDRESS, &i2c_slave_handler);

    sleep_ms(2000);
    printf("Shrike RP2040 I2C Slave Online (Address 0x42)\n");

    while (1) {
        // Safely print data outside the interrupt
        if (rx_tail != rx_head) {
            printf("Received from FPGA: 0x%02X\n", rx_buf[rx_tail]);
            rx_tail = (rx_tail + 1) % BUF_SIZE;
        }
        if (tx_tail != tx_head) {
            printf("Sent to FPGA: 0x%02X\n", tx_buf[tx_tail]);
            tx_tail = (tx_tail + 1) % BUF_SIZE;
        }
    }
    return 0;
}
