#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "irq.h"
#include "isr.h"

#define RP2040_I2C_ADDR 0x42

void print_char(char c) {
    while(uart_txfull_read()); 
    uart_rxtx_write(c);
}

void print_str(const char *str) {
    while(*str) print_char(*str++);
}

void print_hex8(uint8_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    print_char(hex_chars[(val >> 4) & 0x0F]);
    print_char(hex_chars[val & 0x0F]);
}

void system_init(void) {
    portb_oe_write(0xFF); // Port B all outputs (LEDs)
    portc_oe_write(0x00);
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0); timer0_load_write(0); timer0_reload_write(ticks_per_ms);
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(CSR_TIMER0_EV_ENABLE_ZERO_SIZE); timer0_en_write(1);

    // [FIX 1] Enable the I2C Master state machine
    i2c_master_active_write(1);

    spi_master_phyconfig_write((8 << CSR_SPI_MASTER_PHYCONFIG_LEN_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_WIDTH_OFFSET) | 
                               (1 << CSR_SPI_MASTER_PHYCONFIG_MASK_OFFSET));

    irq_setmask((1 << TIMER0_INTERRUPT) | (1 << SPI_INTERRUPT) | (1 << I2C_INTERRUPT));
    irq_setie(1);
}

int main(void) {
    system_init();
    print_str("\r\n=== Duplex Communication Test ===\r\n");
    print_str("SW1: 0=SPI, 1=I2C\r\n");
    print_str("SW2: (I2C Only) 0=Receive, 1=Send\r\n");
    print_str("SW3: 0=Polling, 1=Interrupt\r\n");

    uint8_t fpga_spi_tx_counter = 0xA0; 
    uint8_t fpga_i2c_tx_counter = 0xB0; 
    uint8_t led_state = 0;

    while(1) {
        uint32_t sw = portc_in_read();
        uint8_t bus_is_i2c = sw & 0x02; // SW1
        uint8_t i2c_send   = sw & 0x04; // SW2
        uint8_t use_int    = sw & 0x08; // SW3

        // Process interrupt flags
        if (spi_int_fired) {
            spi_int_fired = 0;
            print_str("[SPI-INT] RX from Shrike: 0x"); print_hex8(spi_rx_data_int); print_str("\r\n");
        }
        if (i2c_int_fired) {
            i2c_int_fired = 0;
            if (!i2c_send) {
                print_str("[I2C-INT] RX from Shrike: 0x"); print_hex8(i2c_rx_data_int); print_str("\r\n");
            }
        }

        if (flag_1sec) {
            flag_1sec = 0;
            led_state++; portb_out_write(led_state);

            if (use_int) {
                spi_master_ev_enable_write(CSR_SPI_MASTER_EV_ENABLE_RX_READY_SIZE);
                i2c_master_ev_enable_write(CSR_I2C_MASTER_EV_ENABLE_RX_READY_SIZE);
            } else {
                spi_master_ev_enable_write(0);
                i2c_master_ev_enable_write(0);
            }

            if (!bus_is_i2c) { 
                // --- FULL DUPLEX SPI ---
                fpga_spi_tx_counter++;
                print_str("[SPI] TX: 0x"); print_hex8(fpga_spi_tx_counter); print_str("\r\n");
                
                spi_master_cs_write(1); 
                spi_master_rxtx_write(fpga_spi_tx_counter);
                
                if (!use_int) {
                    while(!(spi_master_status_read() & (1 << CSR_SPI_MASTER_STATUS_RX_READY_OFFSET)));
                    uint8_t rx_byte = spi_master_rxtx_read(); 
                    spi_master_cs_write(0); 
                    print_str("[SPI-POL] RX from Shrike: 0x"); print_hex8(rx_byte); print_str("\r\n");
                }
            } else { 
                // --- HALF DUPLEX I2C ---
                i2c_master_addr_write(RP2040_I2C_ADDR);
                
                if (i2c_send) {
                    fpga_i2c_tx_counter++;
                    print_str("[I2C] TX: 0x"); print_hex8(fpga_i2c_tx_counter); print_str("\r\n");
                    
                    i2c_master_settings_write((1 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET) | (0 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET));
                    i2c_master_rxtx_write(fpga_i2c_tx_counter);
                    
                    if (!use_int) {
                        // [FIX 2] Poll TX_UNFINISHED to ensure the bus transaction is physically complete
                        while(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_TX_UNFINISHED_OFFSET));
                    }
                } else { 
                    i2c_master_settings_write((0 << CSR_I2C_MASTER_SETTINGS_LEN_TX_OFFSET) | (1 << CSR_I2C_MASTER_SETTINGS_LEN_RX_OFFSET));
                    
                    // [FIX 3] Restore the dummy write. This triggers the hardware to clock out the READ request.
                    i2c_master_rxtx_write(0x00);
                    
                    if (!use_int) {
                        while(!(i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_RX_READY_OFFSET)));
                        
                        if (i2c_master_status_read() & (1 << CSR_I2C_MASTER_STATUS_NACK_OFFSET)) {
                            print_str("[I2C-POL] NACK\r\n");
                        } else {
                            uint8_t rx_byte = i2c_master_rxtx_read();
                            print_str("[I2C-POL] RX from Shrike: 0x"); print_hex8(rx_byte); print_str("\r\n");
                        }
                    }
                }
            }
        }
    }
    return 0;
}
