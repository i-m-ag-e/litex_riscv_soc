// =============================================================================
// test_blink.c  —  Test 1: Bootloader-Survived Blink
// =============================================================================
// Purpose:
//   Prove the full boot path works end-to-end:
//     1. Bootloader received this binary via UART (or from flash on warm boot)
//     2. BSS section was zeroed by app_start.S
//     3. jump_to_app() hit MAIN_RAM_BASE and control reached main()
//
// Visual output (Basys3):
//   LEDs (PORTB) alternate between 0xAA / 0x55 roughly every 500 ms.
//   A steady 0xAA / 0x55 pattern confirms clean execution.
//   If the board is frozen on 0x01 or 0x04 the bootloader never jumped.
//
// Hardware:
//   PORTB = 8-bit LED output (LD7..LD0)
//   PINC  = slider switches (SW7..SW0) — SW0 used by bootloader for mode,
//           not used in this test.
// =============================================================================

#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>

// BSS-cleared global — must read 0 on entry; proves CRT ran correctly.
volatile uint32_t bss_check;

// Delay loop — calibrated very roughly for a 75 MHz PicoRV32.
// ~75e6 / 4 iters per loop ≈ ~18.75M loops per second → 500 ms ≈ 9.375M loops.
#define HALF_SECOND_LOOPS 9375000UL

static void delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
        __asm__ volatile("nop");
    }
}

// ISR stub — interrupts not used in this test.
void isr(void) {}

int main(void) {
    // ── Test 1: Confirm BSS was zeroed ──────────────────────────────────────
    // bss_check lives in .bss.  If app_start.S's BSS-clear loop is broken,
    // this will contain leftover flash garbage and the LED pattern will
    // start at 0xFF instead of 0x00, which is immediately visible.
    if (bss_check != 0) {
        // BSS not cleared — show error pattern: all LEDs on, frozen.
        portb_out_write(0xFF);
        while (1);
    }

    // ── Test 2: Basic execution + loop ──────────────────────────────────────
    uint8_t pattern = 0xAA;

    for (;;) {
        portb_out_write(pattern);
        delay(HALF_SECOND_LOOPS);
        pattern ^= 0xFF;   // toggle between 0xAA and 0x55
    }

    return 0;
}
