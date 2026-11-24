# ----------------------------------------------------------------------------
# Clock and Reset
# ----------------------------------------------------------------------------
set_property PACKAGE_PIN W5 [get_ports clk100]
set_property IOSTANDARD LVCMOS33 [get_ports clk100]
create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports clk100]

# Center Button for Reset
set_property PACKAGE_PIN U18 [get_ports btnC]
set_property IOSTANDARD LVCMOS33 [get_ports btnC]

set_property PACKAGE_PIN T17 [get_ports prog_mode]
set_property IOSTANDARD LVCMOS33 [get_ports prog_mode]

# ----------------------------------------------------------------------------
# Main UART (Connected to USB port for terminal output)
# ----------------------------------------------------------------------------
set_property PACKAGE_PIN B18 [get_ports RsRx]
set_property IOSTANDARD LVCMOS33 [get_ports RsRx]
set_property PACKAGE_PIN A18 [get_ports RsTx]
set_property IOSTANDARD LVCMOS33 [get_ports RsTx]

# ----------------------------------------------------------------------------
# Wire-Free Testing: Port B mapped to 8 Onboard LEDs
# ----------------------------------------------------------------------------
set_property PACKAGE_PIN U16 [get_ports {portb_pin[0]}]
set_property PACKAGE_PIN E19 [get_ports {portb_pin[1]}]
set_property PACKAGE_PIN U19 [get_ports {portb_pin[2]}]
set_property PACKAGE_PIN V19 [get_ports {portb_pin[3]}]
set_property PACKAGE_PIN W18 [get_ports {portb_pin[4]}]
set_property PACKAGE_PIN U15 [get_ports {portb_pin[5]}]
set_property PACKAGE_PIN U14 [get_ports {portb_pin[6]}]
set_property PACKAGE_PIN V14 [get_ports {portb_pin[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {portb_pin[*]}]

# ----------------------------------------------------------------------------
# Wire-Free Testing: Port C mapped to 8 Onboard Slide Switches
# ----------------------------------------------------------------------------
set_property PACKAGE_PIN V17 [get_ports {portc_pin[0]}]
set_property PACKAGE_PIN V16 [get_ports {portc_pin[1]}]
set_property PACKAGE_PIN W16 [get_ports {portc_pin[2]}]
set_property PACKAGE_PIN W17 [get_ports {portc_pin[3]}]
set_property PACKAGE_PIN W15 [get_ports {portc_pin[4]}]
set_property PACKAGE_PIN V15 [get_ports {portc_pin[5]}]
set_property PACKAGE_PIN W14 [get_ports {portc_pin[6]}]
set_property PACKAGE_PIN W13 [get_ports {portc_pin[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {portc_pin[*]}]

# ----------------------------------------------------------------------------
# PMOD Mapping for the remaining generic ports (To prevent Vivado Errors)
# ----------------------------------------------------------------------------
# We will map these to PMOD headers just to satisfy the synthesis tool.
# You don't need to connect any wires to them to test the LEDs and Switches.

# PMOD A (JC) - SPI
set_property PACKAGE_PIN K17 [get_ports spi_cs_n]
set_property PACKAGE_PIN M18 [get_ports spi_mosi]
set_property PACKAGE_PIN N17 [get_ports spi_miso]
set_property PULLUP true [get_ports spi_miso]
set_property PACKAGE_PIN P18 [get_ports spi_clk]
set_property IOSTANDARD LVCMOS33 [get_ports spi_*]

# PMOD B (JB) - I2C and UART1
set_property PACKAGE_PIN A14 [get_ports i2c_scl]
set_property PACKAGE_PIN A16 [get_ports i2c_sda]
set_property PULLUP true [get_ports i2c_scl]
set_property PULLUP true [get_ports i2c_sda]
set_property PACKAGE_PIN B15 [get_ports uart1_tx]
set_property PACKAGE_PIN B16 [get_ports uart1_rx]
set_property PULLUP true [get_ports uart1_rx]
set_property IOSTANDARD LVCMOS33 [get_ports i2c_*]
set_property IOSTANDARD LVCMOS33 [get_ports uart1_*]

# PMOD C (JA) - PWM and Port D
set_property PACKAGE_PIN J1 [get_ports pwm_ch_a]
set_property PACKAGE_PIN L2 [get_ports pwm_ch_b]
set_property PACKAGE_PIN J2 [get_ports pwm_ch_c]
set_property PACKAGE_PIN G2 [get_ports pwm_ch_d]
set_property IOSTANDARD LVCMOS33 [get_ports pwm_*]

set_property PACKAGE_PIN H1 [get_ports {portd_pin[0]}]
set_property PACKAGE_PIN K2 [get_ports {portd_pin[1]}]
set_property PACKAGE_PIN H2 [get_ports {portd_pin[2]}]
set_property PACKAGE_PIN G3 [get_ports {portd_pin[3]}]
set_property PACKAGE_PIN J3 [get_ports {portd_pin[4]}]
set_property PACKAGE_PIN L3 [get_ports {portd_pin[5]}]
set_property PACKAGE_PIN M2 [get_ports {portd_pin[6]}]
set_property PACKAGE_PIN M3 [get_ports {portd_pin[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {portd_pin[*]}]

# ----------------------------------------------------------------------------
# Dedicated Onboard SPI Flash (Spansion)
# ----------------------------------------------------------------------------
set_property PACKAGE_PIN K19 [get_ports spiflash_cs_n]
set_property PACKAGE_PIN D18 [get_ports spiflash_mosi]
set_property PACKAGE_PIN D19 [get_ports spiflash_miso]
set_property PACKAGE_PIN G18 [get_ports spiflash_wp_n]
set_property PACKAGE_PIN F18 [get_ports spiflash_hold_n]
set_property IOSTANDARD LVCMOS33 [get_ports spiflash_wp_n]
set_property IOSTANDARD LVCMOS33 [get_ports spiflash_hold_n]
set_property IOSTANDARD LVCMOS33 [get_ports spiflash_*]

# Allow Vivado to map the configuration pins as standard I/O after boot
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]
