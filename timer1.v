// ============================================================================
// timer1.v — 16-bit Multi-Channel PWM Timer (ASIC-Optimized)
//
// Architecture:  Shared-counter, 4-channel Fast PWM engine
// Reset:         Synchronous active-high (STA-clean for Yosys/OpenLane)
// Bus:           None — raw configuration wires for LiteX AutoCSR binding
// Buffering:     Shadow-buffered compare registers AND top value
//                (glitch-free duty cycle and period updates)
// ============================================================================

module timer1 #(
    parameter WIDTH = 16           // Counter width (8, 16, or 32)
) (
    input  wire             clk,
    input  wire             rst,           // Synchronous active-high reset

    // Configuration inputs (from LiteX CSRs)
    input  wire             ctrl_en,       // Master enable: 1 = counting, 0 = frozen
    input  wire [WIDTH-1:0] top_val,       // PWM period (TOP value)
    input  wire [WIDTH-1:0] cc_a,          // Channel A compare (duty cycle)
    input  wire [WIDTH-1:0] cc_b,          // Channel B compare (duty cycle)
    input  wire [WIDTH-1:0] cc_c,          // Channel C compare (duty cycle)
    input  wire [WIDTH-1:0] cc_d,          // Channel D compare (duty cycle)

    // Status outputs (to LiteX CSRs)
    output reg  [WIDTH-1:0] tcnt_out,      // Counter snapshot (1-cycle behind r_tcnt)

    // PWM output pins
    output wire             oc_a,          // Channel A output
    output wire             oc_b,          // Channel B output
    output wire             oc_c,          // Channel C output
    output wire             oc_d,          // Channel D output

    // Interrupt
    output reg              ov_irq         // 1-cycle pulse at period boundary
);

    // ========================================================================
    // Internal Registers
    // ========================================================================

    // Core up-counter
    reg [WIDTH-1:0] r_tcnt;

    // Shadow-buffered TOP value (period changes deferred to next wrap).
    // Note: resets to 0, but the disabled-preload phase (or the first
    // enabled cycle's r_tcnt==0 capture) loads the real top_val before
    // the counter ever increments. See counter logic for details.
    reg [WIDTH-1:0] shadow_top;

    // Shadow-buffered compare registers (active copies used by hardware)
    reg [WIDTH-1:0] shadow_cc_a;
    reg [WIDTH-1:0] shadow_cc_b;
    reg [WIDTH-1:0] shadow_cc_c;
    reg [WIDTH-1:0] shadow_cc_d;

    // Wire array view of shadow CCs (for loop-based PWM logic)
    wire [WIDTH-1:0] shadow_cc_arr [0:3];
    assign shadow_cc_arr[0] = shadow_cc_a;
    assign shadow_cc_arr[1] = shadow_cc_b;
    assign shadow_cc_arr[2] = shadow_cc_c;
    assign shadow_cc_arr[3] = shadow_cc_d;

    // PWM output register (bits mapped to oc_a..oc_d)
    reg [3:0] oc_out;
    assign oc_a = oc_out[0];
    assign oc_b = oc_out[1];
    assign oc_c = oc_out[2];
    assign oc_d = oc_out[3];

    // ========================================================================
    // Counter + Shadow Buffer Logic
    //
    // All shadow registers (top + compare) capture their input values
    // whenever r_tcnt == 0, which occurs in three scenarios:
    //   1. During reset (shadows cleared to 0)
    //   2. While disabled (shadows preloaded with current inputs)
    //   3. At the counter wrap point (shadows updated for next period)
    //
    // This guarantees glitch-free updates to both duty cycle and period.
    // ========================================================================
    always @(posedge clk) begin
        if (rst) begin
            r_tcnt      <= {WIDTH{1'b0}};
            shadow_top  <= {WIDTH{1'b0}};
            shadow_cc_a <= {WIDTH{1'b0}};
            shadow_cc_b <= {WIDTH{1'b0}};
            shadow_cc_c <= {WIDTH{1'b0}};
            shadow_cc_d <= {WIDTH{1'b0}};
        end
        else if (!ctrl_en) begin
            // Disabled: hold counter at zero and preload all shadows
            r_tcnt      <= {WIDTH{1'b0}};
            shadow_top  <= top_val;
            shadow_cc_a <= cc_a;
            shadow_cc_b <= cc_b;
            shadow_cc_c <= cc_c;
            shadow_cc_d <= cc_d;
        end
        else begin
            // Enabled: count up, wrap at shadow_top
            if (r_tcnt >= shadow_top)
                r_tcnt <= {WIDTH{1'b0}};
            else
                r_tcnt <= r_tcnt + {{(WIDTH-1){1'b0}}, 1'b1};

            // Capture all shadows at period boundary (r_tcnt == 0).
            // Also fires on the very first enabled cycle (counter starts
            // at 0 from the disabled/reset state).
            if (r_tcnt == {WIDTH{1'b0}}) begin
                shadow_top  <= top_val;
                shadow_cc_a <= cc_a;
                shadow_cc_b <= cc_b;
                shadow_cc_c <= cc_c;
                shadow_cc_d <= cc_d;
            end
        end
    end

    // ========================================================================
    // Counter Snapshot Readback (1-cycle latency from r_tcnt)
    //
    // Software reading tcnt_out via LiteX CSR sees the counter value from
    // the previous clock cycle. This is inherent to registered readback
    // and ensures clean timing for the CSR bus.
    //
    // Timing note: when ov_irq fires, tcnt_out will read shadow_top (the
    // TOP value), not 0. The wrap to 0 appears in tcnt_out one cycle later.
    // ========================================================================
    always @(posedge clk) begin
        if (rst)
            tcnt_out <= {WIDTH{1'b0}};
        else
            tcnt_out <= r_tcnt;
    end

    // ========================================================================
    // Overflow / Period Boundary IRQ (1-cycle pulse)
    //
    // Asserted for exactly 1 clock cycle when the counter wraps from a
    // non-zero value. Suitable for LiteX EventManager (rising-edge).
    //
    // The r_tcnt != 0 guard prevents a spurious pulse on the very first
    // enabled cycle (when r_tcnt is still 0 from reset/disabled state).
    // Trade-off: no IRQ when top_val == 0 (1-cycle period), which is
    // acceptable since a per-cycle interrupt would flood the CPU.
    // ========================================================================
    always @(posedge clk) begin
        if (rst)
            ov_irq <= 1'b0;
        else
            ov_irq <= ctrl_en
                   && (r_tcnt >= shadow_top)
                   && (r_tcnt != {WIDTH{1'b0}});
    end

    // ========================================================================
    // PWM Waveform Generation — Fast PWM (Non-Inverting)
    //
    // Behavior per channel:
    //   - When r_tcnt == 0: set HIGH (unless shadow_cc == 0 → stay LOW)
    //   - When r_tcnt == shadow_cc: set LOW
    //   - Reset / disabled: outputs driven LOW
    //
    // All outputs are fully registered (no combinatorial paths to pins).
    //
    // Edge cases (by design, consistent with standard PWM peripherals):
    //   - cc == 0       → 0% duty cycle (output permanently LOW)
    //   - cc > top      → ~100% duty cycle (match never fires, stays HIGH)
    //   - cc == top     → output LOW for 1 cycle at wrap = TOP/(TOP+1) duty
    // ========================================================================
    // Note: integer i declared at module scope because Verilog-2001 does
    // not support block-local variable declarations. Safe for synthesis
    // (loop is fully unrolled); no multi-driver risk since only one
    // always block uses it.
    integer i;
    always @(posedge clk) begin
        if (rst)
            oc_out <= 4'b0000;
        else if (!ctrl_en)
            oc_out <= 4'b0000;
        else begin
            for (i = 0; i < 4; i = i + 1) begin
                if (r_tcnt == {WIDTH{1'b0}})
                    oc_out[i] <= (shadow_cc_arr[i] != {WIDTH{1'b0}});
                else if (r_tcnt == shadow_cc_arr[i])
                    oc_out[i] <= 1'b0;
            end
        end
    end

endmodule
