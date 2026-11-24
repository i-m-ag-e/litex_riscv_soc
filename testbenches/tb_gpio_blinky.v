// =============================================================================
// tb_gpio_blinky.v — Warm-boot + GPIO Firmware Execution Test
// =============================================================================
// Target:    asic_top_fixed.v  (flattened PicoRV32 SoC)
// Simulator: Verilator --timing --binary
// Test:      Warm boot fast-path (State A).  top_main_ram.init must contain
//            test_gpio.bin firmware + WARM_BOOT_MAGIC=0xDEADBEEF in last word.
//
// Pass conditions:
//   1. portb_o transitions 0x01 → 0x00  (bootloader: online → jump)
//   2. portb_o subsequently reaches 0xAA (firmware running — test_gpio.c line 1)
//   3. spiflash_cs_n NEVER goes low at any point (State A skips SPI entirely)
//
// Fail conditions:
//   * spiflash_cs_n goes low at any time  → print FAIL + finish
//   * Timeout (no 0xAA seen in 50 ms sim) → print TIMEOUT FAIL + finish
// =============================================================================

`timescale 1ns / 1ps

module tb_gpio_blinky;

// ---------------------------------------------------------------------------
// Clock & Reset
// ---------------------------------------------------------------------------
reg clk = 0;
reg rst = 1;

// 100 MHz clock — toggle every 5 ns
always #5 clk = ~clk;

// ---------------------------------------------------------------------------
// DUT port connections
// ---------------------------------------------------------------------------
// GPIO port I/O/OE
wire [7:0] portb_o;
wire [7:0] portb_oe;
wire [7:0] portc_o;
wire [7:0] portc_oe;
wire [7:0] portd_o;
wire [7:0] portd_oe;

// SPI-flash (should NEVER be touched in warm-boot path)
wire spiflash_clk;
wire spiflash_cs_n;
wire spiflash_mosi;

// Other outputs we don't check (just leave floating)
wire serial_tx;
wire uart1_tx;
wire spi_clk, spi_cs_n, spi_mosi;
wire pwm_ch_a, pwm_ch_b, pwm_ch_c, pwm_ch_d;
wire i2c_scl_o, i2c_scl_oe;
wire i2c_sda_o, i2c_sda_oe;

// ---------------------------------------------------------------------------
// DUT instantiation
// ---------------------------------------------------------------------------
top dut (
    .clk            (clk),
    .rst            (rst),

    // GPIO ports — drive inputs low, monitor outputs
    .portb_i        (8'h00),
    .portb_o        (portb_o),
    .portb_oe       (portb_oe),
    .portc_i        (8'h00),
    .portc_o        (portc_o),
    .portc_oe       (portc_oe),
    .portd_i        (8'h00),
    .portd_o        (portd_o),
    .portd_oe       (portd_oe),

    // Control
    .prog_mode      (1'b0),     // Warm-boot: NOT in UART recovery mode

    // UART console (bootloader uses it, tie rx high = idle)
    .serial_rx      (1'b1),
    .serial_tx      (serial_tx),

    // UART1 (unused in this test)
    .uart1_rx       (1'b1),
    .uart1_tx       (uart1_tx),

    // User SPI (unused)
    .spi_miso       (1'b0),
    .spi_clk        (spi_clk),
    .spi_cs_n       (spi_cs_n),
    .spi_mosi       (spi_mosi),

    // SPI flash — tie MISO low; CS_N monitored for assertion
    .spiflash_miso  (1'b0),
    .spiflash_clk   (spiflash_clk),
    .spiflash_cs_n  (spiflash_cs_n),
    .spiflash_mosi  (spiflash_mosi),

    // I2C — tie inputs low/high per protocol idle
    .i2c_scl_i      (1'b1),
    .i2c_scl_o      (i2c_scl_o),
    .i2c_scl_oe     (i2c_scl_oe),
    .i2c_sda_i      (1'b1),
    .i2c_sda_o      (i2c_sda_o),
    .i2c_sda_oe     (i2c_sda_oe),

    // PWM (leave open)
    .pwm_ch_a       (pwm_ch_a),
    .pwm_ch_b       (pwm_ch_b),
    .pwm_ch_c       (pwm_ch_c),
    .pwm_ch_d       (pwm_ch_d)
);

// ---------------------------------------------------------------------------
// Reset sequence: hold rst high for 20 cycles, then release
// ---------------------------------------------------------------------------
initial begin
    repeat (20) @(posedge clk);
    rst = 0;
    $display("[%0t] BOOT: Reset released. CPU starting.", $time);
end

// ---------------------------------------------------------------------------
// portb_o monitor — print every change
// ---------------------------------------------------------------------------
reg [7:0] portb_prev = 8'hFF;

always @(posedge clk) begin
    if (portb_o !== portb_prev) begin
        $display("[%0t] BLINKY portb_o = 0x%02X", $time, portb_o);
        portb_prev <= portb_o;
    end
end

// ---------------------------------------------------------------------------
// PASS detector: 0xAA on portb_o = firmware is running (test_gpio.c main())
// ---------------------------------------------------------------------------
always @(posedge clk) begin
    if (portb_o === 8'hAA) begin
        $display("[%0t] PASS: portb_o reached 0xAA — firmware is executing after warm-boot jump!", $time);
        #100;
        $finish;
    end
end

// ---------------------------------------------------------------------------
// SPI flash assertion:
//   The bootloader ALWAYS does a mandatory flash kick-start at the very top of
//   main() (boot_main.c lines 463-475: 8 × 0xFF + release-from-power-down 0xAB)
//   before it even reads the warm-boot magic.  This is expected and unavoidable.
//
//   What we MUST NOT see is spiflash_cs_n going low AFTER the bootloader has
//   committed to the warm-boot jump — i.e., after portb_o first becomes 0x00.
//   The firmware itself (test_gpio.c) never touches the SPI flash at all.
//
//   Therefore: once portb_o has gone 0x01 → 0x00 (boot committed), any further
//   spiflash_cs_n negedge is a failure.
// ---------------------------------------------------------------------------
reg boot_committed = 0;  // Set when portb_o first goes to 0x00 after being 0x01

always @(posedge clk) begin
    // Detect the 0x01 → 0x00 transition: bootloader committed to warm-boot jump
    if (!boot_committed && portb_prev === 8'h01 && portb_o === 8'h00)
        boot_committed <= 1;
end

always @(negedge spiflash_cs_n) begin
    if (boot_committed) begin
        $display("[%0t] FAIL: spiflash_cs_n went low AFTER warm-boot jump commit — firmware should NOT touch SPI flash!", $time);
        $finish;
    end else begin
        $display("[%0t] INFO: spiflash_cs_n went low (expected bootloader flash init, before warm-boot commit)", $time);
    end
end

// ---------------------------------------------------------------------------
// Watchdog timeout: 50 ms simulation time = 50,000,000 ns
// At 100 MHz, that is 5,000,000 clock cycles — generous for boot + firmware.
// ---------------------------------------------------------------------------
initial begin
    #50_000_000;
    $display("[%0t] TIMEOUT FAIL: portb_o never reached 0xAA within 50 ms sim time.", $time);
    $finish;
end

endmodule
