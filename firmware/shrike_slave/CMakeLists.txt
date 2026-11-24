cmake_minimum_required(VERSION 3.13)

# Initialize the SDK
include(pico_sdk_import.cmake)
project(shrike_slave_project C CXX ASM)
pico_sdk_init()

# Create the executable
add_executable(shrike_slave main.c)

# Pull in the hardware libraries for SPI and UART
target_link_libraries(shrike_slave pico_stdlib hardware_uart hardware_spi hardware_irq hardware_i2c pico_i2c_slave)

# Enable USB serial and disable standard UART serial for printf (optional, but good practice)
pico_enable_stdio_usb(shrike_slave 1)
pico_enable_stdio_uart(shrike_slave 0)

# Generate the .uf2 file
pico_add_extra_outputs(shrike_slave)
