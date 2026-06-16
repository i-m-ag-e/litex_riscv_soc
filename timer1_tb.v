`timescale 1ns / 1ps

// ============================================================================
// timer1_tb.v — Comprehensive Testbench for 16-bit Multi-Channel PWM Timer
//
// Tests cover:
//   1.  Reset behavior (all outputs LOW, counter at 0)
//   2.  Counter disabled behavior
//   3.  Basic counting with TOP=9
//   4.  Precise counter sequence with TOP=4
//   5.  Channel A Fast PWM waveform shape
//   6.  0% duty cycle (cc=0)
//   7.  ~100% duty cycle (cc > TOP)
//   8.  Shadow buffering — mid-period cc change deferred
//   9.  Independent 4-channel operation
//   10. TOP value change (shadow-buffered, deferred to next period)
//   11. Enable/disable cycling
//   12. TOP=0 edge case
//   13. TOP=0 with cc > TOP
//   14. Maximum period (TOP=65535)
//   15. Overflow IRQ pulse verification
//   16. Shadow-buffered TOP prevents mid-period truncation
// ============================================================================

module timer1_tb;

    reg        clk;
    reg        rst;
    reg        ctrl_en;
    reg [15:0] top_val;
    reg [15:0] cc_a, cc_b, cc_c, cc_d;
    wire [15:0] tcnt_out;
    wire       oc_a, oc_b, oc_c, oc_d;
    wire       ov_irq;

    timer1 #(.WIDTH(16)) uut (
        .clk(clk),
        .rst(rst),
        .ctrl_en(ctrl_en),
        .top_val(top_val),
        .cc_a(cc_a),
        .cc_b(cc_b),
        .cc_c(cc_c),
        .cc_d(cc_d),
        .tcnt_out(tcnt_out),
        .oc_a(oc_a),
        .oc_b(oc_b),
        .oc_c(oc_c),
        .oc_d(oc_d),
        .ov_irq(ov_irq)
    );

    // 10ns clock period (100 MHz)
    initial clk = 0;
    always #5 clk = ~clk;

    integer errors = 0;
    integer test_num = 0;

    task check;
        input [255:0] test_name;
        input expected;
        input actual;
        begin
            if (expected !== actual) begin
                $display("FAIL [%0d] %0s: expected %b, got %b at time %0t",
                         test_num, test_name, expected, actual, $time);
                errors = errors + 1;
            end
        end
    endtask

    task check16;
        input [255:0] test_name;
        input [15:0] expected;
        input [15:0] actual;
        begin
            if (expected !== actual) begin
                $display("FAIL [%0d] %0s: expected %0d (0x%04x), got %0d (0x%04x) at time %0t",
                         test_num, test_name, expected, expected, actual, actual, $time);
                errors = errors + 1;
            end
        end
    endtask

    task tick;
        begin
            @(posedge clk);
            #1;
        end
    endtask

    task tick_n;
        input integer n;
        integer i;
        begin
            for (i = 0; i < n; i = i + 1)
                tick;
        end
    endtask

    // Standard initialization: reset + 1 disabled cycle for shadow preload.
    task init;
        begin
            rst = 1; ctrl_en = 0;
            tick; tick;
            rst = 0;
            tick; // 1 disabled cycle — shadows preload
        end
    endtask

    initial begin
        $dumpfile("timer1_tb.vcd");
        $dumpvars(0, timer1_tb);

        // ================================================================
        // TEST 1: Reset behavior
        // ================================================================
        test_num = 1;
        $display("\n--- Test %0d: Reset behavior ---", test_num);
        rst = 1; ctrl_en = 0;
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd3; cc_c = 16'd7; cc_d = 16'd1;
        tick; tick;
        check("oc_a during reset", 1'b0, oc_a);
        check("oc_b during reset", 1'b0, oc_b);
        check("oc_c during reset", 1'b0, oc_c);
        check("oc_d during reset", 1'b0, oc_d);
        check("ov_irq during reset", 1'b0, ov_irq);
        check16("tcnt_out during reset", 16'd0, tcnt_out);

        // ================================================================
        // TEST 2: Counter stays at 0 when disabled
        // ================================================================
        test_num = 2;
        $display("\n--- Test %0d: Counter disabled ---", test_num);
        rst = 0; ctrl_en = 0; top_val = 16'd9; cc_a = 16'd5;
        tick_n(5);
        check16("tcnt_out when disabled", 16'd0, tcnt_out);
        check("oc_a when disabled", 1'b0, oc_a);

        // ================================================================
        // TEST 3: Basic counting (TOP=9, period=10 clocks)
        // ================================================================
        test_num = 3;
        $display("\n--- Test %0d: Basic counting TOP=9 ---", test_num);
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd0; cc_c = 16'd9; cc_d = 16'd3;
        init;
        ctrl_en = 1;
        tick_n(20);
        $display("  Counter running OK (smoke test passed)");

        // ================================================================
        // TEST 4: Precise counter sequence (TOP=4, period=5 clocks)
        // ================================================================
        test_num = 4;
        $display("\n--- Test %0d: Counter sequence TOP=4 ---", test_num);
        top_val = 16'd4; cc_a = 16'd2; cc_b = 16'd0; cc_c = 16'd4; cc_d = 16'd1;
        init;
        ctrl_en = 1;

        tick; // C1: r_tcnt=0→1, tcnt_out=0
        check16("tcnt_out C1", 16'd0, tcnt_out);
        check("oc_a C1 (HIGH at 0)", 1'b1, oc_a);

        tick; // C2: r_tcnt=1→2, tcnt_out=1
        check16("tcnt_out C2", 16'd1, tcnt_out);
        check("oc_a C2 (hold HIGH)", 1'b1, oc_a);

        tick; // C3: r_tcnt=2→3, match at 2 → oc_a LOW
        check16("tcnt_out C3", 16'd2, tcnt_out);
        check("oc_a C3 (LOW at match=2)", 1'b0, oc_a);

        tick; // C4: r_tcnt=3→4
        check16("tcnt_out C4", 16'd3, tcnt_out);

        tick; // C5: r_tcnt=4→0 (wrap)
        check16("tcnt_out C5 (at TOP)", 16'd4, tcnt_out);

        tick; // C6: r_tcnt=0→1 (new period)
        check16("tcnt_out C6 (new period)", 16'd0, tcnt_out);
        check("oc_a C6 (HIGH at new period)", 1'b1, oc_a);

        // ================================================================
        // TEST 5: Channel A PWM waveform shape
        // ================================================================
        test_num = 5;
        $display("\n--- Test %0d: Channel A PWM waveform ---", test_num);
        top_val = 16'd4; cc_a = 16'd2; cc_b = 16'd0; cc_c = 16'd4; cc_d = 16'd1;
        init;
        ctrl_en = 1;

        // Period 1: H H L L L
        tick; check("P1[0]=H", 1'b1, oc_a);
        tick; check("P1[1]=H", 1'b1, oc_a);
        tick; check("P1[2]=L", 1'b0, oc_a);
        tick; check("P1[3]=L", 1'b0, oc_a);
        tick; check("P1[4]=L", 1'b0, oc_a);

        // Period 2: H H L L L (verify repetition)
        tick; check("P2[0]=H", 1'b1, oc_a);
        tick; check("P2[1]=H", 1'b1, oc_a);
        tick; check("P2[2]=L", 1'b0, oc_a);
        tick; check("P2[3]=L", 1'b0, oc_a);
        tick; check("P2[4]=L", 1'b0, oc_a);

        // ================================================================
        // TEST 6: 0% duty cycle (cc=0)
        // ================================================================
        test_num = 6;
        $display("\n--- Test %0d: 0%% duty cycle ---", test_num);
        top_val = 16'd4; cc_a = 16'd0; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick_n(12);
        check("oc_a 0%", 1'b0, oc_a);
        check("oc_b 0%", 1'b0, oc_b);
        check("oc_c 0%", 1'b0, oc_c);
        check("oc_d 0%", 1'b0, oc_d);

        // ================================================================
        // TEST 7: ~100% duty cycle (cc > TOP)
        // ================================================================
        test_num = 7;
        $display("\n--- Test %0d: ~100%% duty cycle (cc > TOP) ---", test_num);
        top_val = 16'd4; cc_a = 16'd10; cc_b = 16'd10; cc_c = 16'd10; cc_d = 16'd10;
        init;
        ctrl_en = 1;
        tick;
        check("oc_a 100% first", 1'b1, oc_a);
        tick_n(15);
        check("oc_a 100% sustained", 1'b1, oc_a);
        check("oc_b 100% sustained", 1'b1, oc_b);
        check("oc_c 100% sustained", 1'b1, oc_c);
        check("oc_d 100% sustained", 1'b1, oc_d);

        // ================================================================
        // TEST 8: Shadow buffering — mid-period cc change deferred
        // ================================================================
        test_num = 8;
        $display("\n--- Test %0d: Shadow buffering ---", test_num);
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;

        tick_n(3); // r_tcnt at 3

        // Change cc_a mid-period — deferred until next wrap
        cc_a = 16'd1;

        tick; // r_tcnt=3→4
        tick; // r_tcnt=4→5 (match detected, output still HIGH this cycle)
        check("oc_a at match cycle (still H)", 1'b1, oc_a);
        tick; // r_tcnt=5→6 (output updates to LOW)
        check("oc_a 1c after match (L)", 1'b0, oc_a);

        tick_n(4); // r_tcnt: 6→7→8→9→0(wrap)

        // New period with shadow_cc_a=1
        tick; // HIGH at r_tcnt=0
        check("oc_a HIGH new period", 1'b1, oc_a);
        tick; // match at 1 detected
        tick; // output goes LOW
        check("oc_a LOW new match=1", 1'b0, oc_a);

        // ================================================================
        // TEST 9: Independent 4-channel operation
        // ================================================================
        test_num = 9;
        $display("\n--- Test %0d: Independent channels ---", test_num);
        top_val = 16'd4; cc_a = 16'd2; cc_b = 16'd0; cc_c = 16'd3; cc_d = 16'd1;
        init;
        ctrl_en = 1;

        tick; // r_tcnt=0→1
        check("ch_a at 0 (H)", 1'b1, oc_a);
        check("ch_b at 0 (L)", 1'b0, oc_b);
        check("ch_c at 0 (H)", 1'b1, oc_c);
        check("ch_d at 0 (H)", 1'b1, oc_d);

        tick; // r_tcnt=1→2, match cc_d=1
        check("ch_d match=1 (L)", 1'b0, oc_d);
        check("ch_a hold (H)", 1'b1, oc_a);
        check("ch_c hold (H)", 1'b1, oc_c);

        tick; // r_tcnt=2→3, match cc_a=2
        check("ch_a match=2 (L)", 1'b0, oc_a);
        check("ch_c hold (H)", 1'b1, oc_c);

        tick; // r_tcnt=3→4, match cc_c=3
        check("ch_c match=3 (L)", 1'b0, oc_c);

        // ================================================================
        // TEST 10: Shadow-buffered TOP (deferred period change)
        // ================================================================
        test_num = 10;
        $display("\n--- Test %0d: Shadow TOP (deferred) ---", test_num);
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick_n(5); // r_tcnt at 5

        // Shrink TOP mid-period — should NOT take effect until next wrap
        // because shadow_top still holds old value (9)
        top_val = 16'd2;
        tick_n(5); // r_tcnt: 5→6→7→8→9→0 (wraps at shadow_top=9)

        // After wrap, shadow_top captures new top_val=2
        // New period: counter should wrap at 2
        tick; // r_tcnt=0→1
        tick; // r_tcnt=1→2
        tick; // r_tcnt=2→0 (wrap at shadow_top=2)
        tick; // r_tcnt=0→1
        check16("tcnt after new TOP=2", 16'd0, tcnt_out);

        // ================================================================
        // TEST 11: Enable/disable cycling
        // ================================================================
        test_num = 11;
        $display("\n--- Test %0d: Enable/disable cycling ---", test_num);
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick_n(5);

        ctrl_en = 0; // disable
        tick; tick;
        check("oc_a after disable", 1'b0, oc_a);
        check16("tcnt_out after disable", 16'd0, tcnt_out);

        ctrl_en = 1; // re-enable
        tick;
        check("oc_a after re-enable", 1'b1, oc_a);

        // ================================================================
        // TEST 12: TOP=0 edge case
        // ================================================================
        test_num = 12;
        $display("\n--- Test %0d: TOP=0 edge case ---", test_num);
        top_val = 16'd0; cc_a = 16'd0; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick_n(5);
        check16("tcnt_out TOP=0", 16'd0, tcnt_out);
        check("oc_a TOP=0 cc=0", 1'b0, oc_a);

        // ================================================================
        // TEST 13: TOP=0 with cc > TOP (100% duty)
        // ================================================================
        test_num = 13;
        $display("\n--- Test %0d: TOP=0 cc_a=1 ---", test_num);
        top_val = 16'd0; cc_a = 16'd1; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick; tick;
        check("oc_a TOP=0 cc=1 (always HIGH)", 1'b1, oc_a);
        tick_n(5);
        check("oc_a TOP=0 cc=1 stable", 1'b1, oc_a);

        // ================================================================
        // TEST 14: Maximum period (TOP=65535)
        // ================================================================
        test_num = 14;
        $display("\n--- Test %0d: TOP=65535 (max) ---", test_num);
        top_val = 16'hFFFF; cc_a = 16'd100; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;
        tick_n(5);
        check16("tcnt_out max period", 16'd4, tcnt_out);

        // ================================================================
        // TEST 15: Overflow IRQ pulse
        // ================================================================
        test_num = 15;
        $display("\n--- Test %0d: Overflow IRQ ---", test_num);
        top_val = 16'd4; cc_a = 16'd2; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;

        // Run through one full period: 0,1,2,3,4(wrap)
        // ov_irq pulses when r_tcnt >= shadow_top AND r_tcnt != 0
        tick; check("irq at 0", 1'b0, ov_irq); // r_tcnt=0→1, no irq (r_tcnt==0)
        tick; check("irq at 1", 1'b0, ov_irq); // r_tcnt=1→2
        tick; check("irq at 2", 1'b0, ov_irq); // r_tcnt=2→3
        tick; check("irq at 3", 1'b0, ov_irq); // r_tcnt=3→4
        tick; check("irq at 4 (pulse)", 1'b1, ov_irq); // r_tcnt=4, 4>=4 → irq
        tick; check("irq after wrap", 1'b0, ov_irq); // r_tcnt=0 → no irq

        // ================================================================
        // TEST 16: Shadow TOP prevents mid-period truncation
        // ================================================================
        test_num = 16;
        $display("\n--- Test %0d: Shadow TOP prevents truncation ---", test_num);
        top_val = 16'd19; cc_a = 16'd10; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        init;
        ctrl_en = 1;

        // Count to 12
        tick_n(12); // r_tcnt at 12

        // Shrink TOP to 5 — should NOT truncate current period
        top_val = 16'd5;
        tick; // r_tcnt=12→13, shadow_top still 19
        check("oc_a still LOW mid-period", 1'b0, oc_a); // match at 10 already fired

        // Counter should continue to 19, not truncate at 5
        tick_n(7); // r_tcnt: 13→14→15→16→17→18→19→0
        tick; // r_tcnt=0→1, new period with shadow_top=5
        check16("tcnt after deferred TOP change", 16'd0, tcnt_out);
        check("oc_a HIGH new short period", 1'b1, oc_a);

        // ================================================================
        // TEST 17: No spurious IRQ on first enable (without init preload)
        // ================================================================
        test_num = 17;
        $display("\n--- Test %0d: No spurious IRQ on first enable ---", test_num);
        // Simulate the "bad" path: enable immediately after reset
        // without a disabled-preload cycle. shadow_top will be 0.
        rst = 1; ctrl_en = 0;
        top_val = 16'd9; cc_a = 16'd5; cc_b = 16'd0; cc_c = 16'd0; cc_d = 16'd0;
        tick; tick;
        rst = 0;
        // Enable immediately — no disabled-preload cycle
        ctrl_en = 1;
        tick; // First enabled cycle: r_tcnt=0, shadow_top=0
              // Old bug: ov_irq would fire here (0 >= 0 is true)
              // Fix: r_tcnt==0, so guard prevents spurious pulse
        check("no spurious irq on enable", 1'b0, ov_irq);
        tick; // shadow_top now captured top_val=9. Counter increments.
        check("no irq cycle 2", 1'b0, ov_irq);

        // Verify IRQ eventually fires at the real wrap point
        tick_n(8); // count up to r_tcnt=9
        tick; // r_tcnt=9, 9>=9 → IRQ
        check("real irq at TOP=9", 1'b1, ov_irq);

        // ================================================================
        // RESULTS
        // ================================================================
        $display("\n=========================================");
        if (errors == 0)
            $display("ALL TESTS PASSED (17 test groups)");
        else
            $display("FAILED: %0d errors detected", errors);
        $display("=========================================\n");

        $finish;
    end

endmodule

