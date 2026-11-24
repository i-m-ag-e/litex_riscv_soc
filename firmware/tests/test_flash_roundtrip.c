// =============================================================================
// test_flash_roundtrip.c  —  Test 3: Flash reload counter
// =============================================================================
//
// WHAT THIS TESTS:
//   Tracks how many times the board has warm-rebooted since the last fresh
//   SRAM wipe (FPGA reprogram or power-off).
//
// LED DISPLAY (PORTB) — plain binary count on all 8 LEDs:
//   0x01 = first boot after SRAM wipe → bootloader loaded from flash/UART
//   0x02 = 1 warm reboot since then
//   0x03 = 2 warm reboots
//   0x04 = 3 warm reboots
//   ...and so on. Just read it as a number.
//
// HOW TO RUN:
//   1. Reprogram FPGA (or power-cycle). SW0 HIGH, press BTNC.
//   2. python3 uploader.py /dev/ttyUSB0 firmware.bin   (burn to flash)
//   3. LEDs = 0x01  →  fresh start confirmed.
//   4. SW0 LOW, press BTNC  →  LEDs = 0x02  (warm reboot #1)
//   5. Press BTNC again     →  LEDs = 0x03  (warm reboot #2)
//   6. Reprogram FPGA again →  LEDs = 0x01  (SRAM wiped, flash re-loaded)
//
// CONTROLS (PINC):
//   SW2 (bit 2) — reset counter back to 0x01
//
// Hardware: PORTB = LEDs, PINC = slider switches
// =============================================================================

#include <stdint.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

void isr(void) {}

// Private firmware sentinel — survives warm reboots, gone after SRAM wipe.
// Lives at the very top of SRAM, well above the binary footprint.
#define FW_SENTINEL_ADDR  (MAIN_RAM_BASE + MAIN_RAM_SIZE - 8)
#define FW_COUNTER_ADDR   (MAIN_RAM_BASE + MAIN_RAM_SIZE - 12)
#define FW_SENTINEL_VAL   0x600DC0DEUL

#define FW_SENTINEL  (*(volatile uint32_t *)FW_SENTINEL_ADDR)
#define FW_COUNTER   (*(volatile uint32_t *)FW_COUNTER_ADDR)

static void delay_ms(uint32_t ms) {
    for (uint32_t m = 0; m < ms; m++)
        for (volatile uint32_t i = 0; i < 18750; i++)
            __asm__ volatile("nop");
}

int main(void) {
    uint32_t count;

    if (FW_SENTINEL == FW_SENTINEL_VAL) {
        // Sentinel present → warm reboot, SRAM survived.
        count = FW_COUNTER + 1;
        if (count > 254) count = 254;   // cap below 0xFF so it's never ambiguous
        FW_COUNTER = count;
    } else {
        // Sentinel missing → SRAM was wiped. Fresh start.
        FW_SENTINEL = FW_SENTINEL_VAL;
        FW_COUNTER  = 1;
        count = 1;
    }

    portb_out_write((uint8_t)count);

    for (;;) {
        if (portc_in_read() & 0x04) {   // SW2: reset counter
            FW_COUNTER = 1;
            FW_SENTINEL = FW_SENTINEL_VAL;
            portb_out_write(0x00);
            delay_ms(200);
            portb_out_write(0x01);
            while (portc_in_read() & 0x04)
                delay_ms(10);
        }
        delay_ms(20);
    }

    return 0;
}
