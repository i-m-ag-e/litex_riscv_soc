// =============================================================================
// test_checksum.c  —  Test 2: Flash checksum + warm-boot magic verification
// =============================================================================
//
// WHAT THIS TESTS:
//   1. The bootloader correctly checksums the payload before jumping.
//      (If checksums didn't work, we'd never arrive here — the bootloader
//       would have NAK'd and halted with LEDs = 0xF0.)
//
//   2. The warm-boot magic word (0xDEADBEEF) written by the bootloader at
//      MAIN_RAM_BASE + MAIN_RAM_SIZE - 4 is intact when we arrive.
//
// LED OUTPUT:
//   0x0F  — magic word present and correct   → PASS
//   Fast blink 0xFF/0x00 — magic missing      → FAIL (bootloader bug)
//
// HOW TO RUN:
//   Step 1: Upload with --ram-only first (quick check):
//     make APP=tests/test_checksum firmware.bin
//     python3 uploader.py /dev/ttyUSB0 firmware.bin --ram-only
//     → Expect LEDs = 0x0F
//
//   Step 2: Upload with flash burn (persistence check):
//     python3 uploader.py /dev/ttyUSB0 firmware.bin
//     → During burn: LEDs = 0x08 (burning indicator from bootloader)
//     → After jump: LEDs = 0x0F
//     → Power-cycle (SW0 LOW): bootloader re-reads flash, validates checksum,
//       jumps back → LEDs = 0x0F again (checksum survived power cycle)
//
// CONTROLS:
//   None needed. Just observe the LEDs.
//   If you see 0x0F, both checksum and magic are working.
//   If you see fast blink, the bootloader has a magic-write bug.
//
// Hardware: PORTB = LEDs (LD7..LD0)
// =============================================================================

#include <stdint.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

#define WARM_BOOT_MAGIC    0xDEADBEEF
#define REG_MAIN_RAM_MAGIC (*(volatile uint32_t *)(MAIN_RAM_BASE + MAIN_RAM_SIZE - 4))

void isr(void) {}

static void delay_ms(uint32_t ms) {
    for (uint32_t m = 0; m < ms; m++)
        for (volatile uint32_t i = 0; i < 18750; i++)
            __asm__ volatile("nop");
}

int main(void) {
    if (REG_MAIN_RAM_MAGIC != WARM_BOOT_MAGIC) {
        // Magic not written — bootloader bug. Blink fast to signal failure.
        for (;;) {
            portb_out_write(0xFF);
            delay_ms(100);
            portb_out_write(0x00);
            delay_ms(100);
        }
    }

    // Magic is correct. Hold steady pattern to confirm.
    // Lower nibble only (0x0F) = easy to distinguish from error (0xFF blink)
    // or bootloader states (0x01, 0x02, 0x04, 0x08).
    portb_out_write(0x0F);

    // Do nothing — just hold the LED pattern so it's clearly readable.
    for (;;)
        __asm__ volatile("nop");

    return 0;
}
