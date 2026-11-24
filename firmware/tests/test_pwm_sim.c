// =============================================================================
// test_pwm_sim.c — Headless PWM (timer1) bring-up test (for RTL sim)
// =============================================================================
// Configures the 16-bit multi-channel PWM engine (timer1.v) with a known period
// and four distinct duty cycles, enables it, and signals "running" on portb_o.
//
// The firmware CANNOT observe its own PWM output pins, so the ACTUAL waveform
// check is done by the testbench (tb_pwm.v), which measures the duty cycle of
// pwm_ch_a..d directly. This firmware's job is only to program timer1 and start
// it. The chosen period/duties are shared with the testbench.
//
// portb_o signalling (watched by the testbench):
//   0x33 = firmware entered
//   0x55 = PWM configured and running -> testbench begins measuring the pins
//
// PASS/FAIL is decided by the testbench from the measured duty cycles.
//
// PWM config (must match tb_pwm.v):
//   TOP  = 99   -> period = 100 sys clocks (1 us @ 100 MHz)
//   cc_a = 25   -> 25% duty on ch_a
//   cc_b = 50   -> 50% duty on ch_b
//   cc_c = 75   -> 75% duty on ch_c
//   cc_d = 0    ->  0% duty on ch_d (output stays low)
// =============================================================================
#include <stdint.h>
#include <generated/csr.h>
#include <generated/soc.h>

#define PWM_TOP   99
#define PWM_CC_A  25
#define PWM_CC_B  50
#define PWM_CC_C  75
#define PWM_CC_D   0

int main(void) {
    portb_oe_write(0xFF);
    portb_out_write(0x33);                    // firmware entered

    // Program while disabled: ctrl=0 holds the counter at 0 and preloads the
    // shadow registers (period + all compares) so the first enabled period is
    // already glitch-free.
    timer1_ctrl_write(0);
    timer1_top_write(PWM_TOP);
    timer1_cc_a_write(PWM_CC_A);
    timer1_cc_b_write(PWM_CC_B);
    timer1_cc_c_write(PWM_CC_C);
    timer1_cc_d_write(PWM_CC_D);
    timer1_ctrl_write(1);                     // enable -> PWM starts counting

    portb_out_write(0x55);                    // "running" — tb_pwm.v measures now
    while (1) { }
    return 0;
}
