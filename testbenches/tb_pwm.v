// =============================================================================
// tb_pwm.v — PWM (timer1) bring-up test (UART-UPLOAD variant)
// =============================================================================
// Target:    asic_top_fixed.v  (flattened PicoRV32 SoC)
// Simulator: Verilator --timing  (also XSim-compatible)
// Firmware:  firmware/tests/test_pwm_sim.c
//
// Firmware delivery: UPLOADED over UART0 (prog_mode=1, bootloader State C), NOT
// pre-loaded into main_ram (the SPRAM main_ram macro has no $readmemh).
//   * main_ram starts all zeros  (gen_init.py --mode upload)
//   * payload streamed from pwm_payload.init (word 0 = byte count, then firmware)
//
// The firmware programs timer1 with TOP=99 and duties 25/50/75/0 on ch_a..d,
// then writes 0x55. Unlike the other tests, PASS/FAIL is decided HERE by directly
// measuring the four pwm_ch_* pins — the CPU cannot observe its own PWM outputs.
// Over a 1000-cycle window (10 PWM periods) we count how many cycles each channel
// is high; the count is the duty percentage x10.
//
// Expected high-counts (period = TOP+1 = 100 cycles, 10 periods = 1000 cycles):
//   ch_a = 250   ch_b = 500   ch_c = 750   ch_d = 0     (+/- tolerance)
//
// Pass:  all four channels within tolerance   -> "PWM PASS"
// Fail:  any channel out of tolerance         -> "PWM FAIL"  OR  watchdog timeout
//
// Baud: 115200 (868 clk/bit @ 100 MHz = 8680 ns/bit)
// =============================================================================

`timescale 1ns / 1ps

module tb_pwm;

localparam integer CLKS_PER_BIT  = 868;
localparam integer BIT_PERIOD_NS = CLKS_PER_BIT * 10;   // 8680 ns
localparam integer RAM_WORDS     = 8192;

// PWM expectations (must match test_pwm_sim.c: TOP=99, cc=25/50/75/0)
localparam integer WIN      = 1000;   // measurement window = 10 periods
localparam integer EXP_A    = 250;
localparam integer EXP_B    = 500;
localparam integer EXP_C    = 750;
localparam integer EXP_D    = 0;
localparam integer TOL      = 25;     // +/- 2.5% of the window

reg clk = 0;
reg rst = 1;
always #5 clk = ~clk;                       // 100 MHz

// DUT ports
wire [7:0] portb_o, portb_oe, portc_o, portc_oe, portd_o, portd_oe;
wire serial_tx;
reg  serial_rx = 1;
wire uart1_tx;
wire spi_clk, spi_cs_n, spi_mosi;
wire spiflash_clk, spiflash_cs_n, spiflash_mosi;
wire pwm_ch_a, pwm_ch_b, pwm_ch_c, pwm_ch_d;
wire i2c_scl_o, i2c_scl_oe, i2c_sda_o, i2c_sda_oe;

// -----------------------------------------------------------------------------
// DUT — prog_mode=1 (State C UART upload). PWM pins observed directly.
// -----------------------------------------------------------------------------
top dut (
    .clk(clk), .rst(rst), .prog_mode(1'b1),
    .portb_i(8'h00), .portb_o(portb_o), .portb_oe(portb_oe),
    .portc_i(8'h00), .portc_o(portc_o), .portc_oe(portc_oe),
    .portd_i(8'h00), .portd_o(portd_o), .portd_oe(portd_oe),
    .serial_rx(serial_rx), .serial_tx(serial_tx),
    .uart1_rx(1'b1),  .uart1_tx(uart1_tx),
    .spi_miso(1'b0),
    .spi_clk(spi_clk), .spi_cs_n(spi_cs_n), .spi_mosi(spi_mosi),
    .spiflash_miso(1'b0),
    .spiflash_clk(spiflash_clk), .spiflash_cs_n(spiflash_cs_n), .spiflash_mosi(spiflash_mosi),
    .i2c_scl_i(1'b1), .i2c_scl_o(i2c_scl_o), .i2c_scl_oe(i2c_scl_oe),
    .i2c_sda_i(1'b1), .i2c_sda_o(i2c_sda_o), .i2c_sda_oe(i2c_sda_oe),
    .pwm_ch_a(pwm_ch_a), .pwm_ch_b(pwm_ch_b), .pwm_ch_c(pwm_ch_c), .pwm_ch_d(pwm_ch_d)
);

// -----------------------------------------------------------------------------
// portb_o monitor
// -----------------------------------------------------------------------------
reg [7:0] portb_prev = 8'hFF;
always @(posedge clk) begin
    if (portb_o !== portb_prev) begin
        $display("[%0t] PWM portb_o = 0x%02X", $time, portb_o);
        portb_prev <= portb_o;
    end
end

// -----------------------------------------------------------------------------
// Duty-cycle measurement + PASS/FAIL (waits for the firmware "running" marker)
// -----------------------------------------------------------------------------
task automatic check_ch;
    input [8*8-1:0] name;   // channel label (up to 8 chars)
    input integer   got;
    input integer   exp;
    inout integer   fails;
    begin
        if ((got > exp + TOL) || (got < exp - TOL)) begin
            $display("[%0t] PWM FAIL: %0s high=%0d/%0d expected ~%0d (tol %0d)",
                     $time, name, got, WIN, exp, TOL);
            fails = fails + 1;
        end else begin
            $display("[%0t] PWM OK:   %0s high=%0d/%0d (expected ~%0d)",
                     $time, name, got, WIN, exp);
        end
    end
endtask

initial begin : measure
    integer i;
    integer ca, cb, cc, cd;
    integer fails;

    // Wait for the firmware to finish programming timer1 and start it.
    while (portb_o !== 8'h55) @(posedge clk);
    $display("[%0t] PWM: firmware running (0x55). Measuring %0d cycles...", $time, WIN);

    ca = 0; cb = 0; cc = 0; cd = 0;
    for (i = 0; i < WIN; i = i + 1) begin
        @(posedge clk);
        if (pwm_ch_a) ca = ca + 1;
        if (pwm_ch_b) cb = cb + 1;
        if (pwm_ch_c) cc = cc + 1;
        if (pwm_ch_d) cd = cd + 1;
    end

    fails = 0;
    check_ch("ch_a", ca, EXP_A, fails);
    check_ch("ch_b", cb, EXP_B, fails);
    check_ch("ch_c", cc, EXP_C, fails);
    check_ch("ch_d", cd, EXP_D, fails);

    if (fails == 0)
        $display("[%0t] PASS: all PWM channels match expected duty (uploaded fw ran).", $time);
    else
        $display("[%0t] FAIL: %0d PWM channel(s) out of tolerance.", $time, fails);
    #100; $finish;
end

// -----------------------------------------------------------------------------
// Watchdog — upload (~105 ms) + firmware run. 800 ms envelope.
// -----------------------------------------------------------------------------
initial begin
    #800_000_000;
    $display("[%0t] TIMEOUT FAIL: firmware never reached 0x55 (PWM never started).", $time);
    $finish;
end

// =============================================================================
// UART firmware-upload machinery (bootloader State C, MODE_RAM) — via UART0
// =============================================================================

// ---- DUT-TX receive FIFO -----------------------------------------------------
localparam RX_FIFO_DEPTH = 32;
reg [7:0] rx_fifo [0:RX_FIFO_DEPTH-1];
reg [4:0] rx_wr = 0;
reg [4:0] rx_rd = 0;
reg       rx_mon_busy = 0;
reg [7:0] rx_mon_byte;

always @(negedge serial_tx) begin
    if (!rx_mon_busy) begin
        rx_mon_busy = 1;
        #(BIT_PERIOD_NS / 2);        // advance to mid-start
        rx_mon_byte = 8'h00;
        begin : rx_bits
            integer si;
            for (si = 0; si < 8; si = si + 1) begin
                #(BIT_PERIOD_NS);
                rx_mon_byte[si] = serial_tx;
            end
        end
        rx_fifo[rx_wr[3:0]] = rx_mon_byte;
        rx_wr = rx_wr + 1;
        $display("[%0t] DUT-TX byte = 0x%02X", $time, rx_mon_byte);
        #(BIT_PERIOD_NS);            // stop bit
        rx_mon_busy = 0;
    end
end

task uart_recv_byte;
    output [7:0] data;
    begin
        while (rx_wr == rx_rd) @(posedge clk);
        data  = rx_fifo[rx_rd[3:0]];
        rx_rd = rx_rd + 1;
    end
endtask

task uart_send_byte;                 // 8N1, LSB first
    input [7:0] data;
    integer i;
    begin
        serial_rx = 0;               // start bit
        #(BIT_PERIOD_NS);
        for (i = 0; i < 8; i = i + 1) begin
            serial_rx = data[i];
            #(BIT_PERIOD_NS);
        end
        serial_rx = 1;               // stop bit
        #(BIT_PERIOD_NS);
    end
endtask

// ---- Firmware payload (self-describing: word 0 = byte count) ------------------
reg [31:0] pay [0:RAM_WORDS];        // [0]=nbytes, [1..]=firmware words
reg [31:0] size_bytes;
integer    nbytes;
reg [31:0] fw_checksum;

initial begin : load_fw
    integer i, w, b;
    reg [7:0] bval;
    $readmemh("pwm_payload.init", pay);
    size_bytes  = pay[0];
    nbytes      = size_bytes;
    fw_checksum = 32'd0;
    for (i = 0; i < nbytes; i = i + 1) begin
        w = i >> 2; b = i & 3;
        bval = pay[1 + w][b*8 +: 8];
        fw_checksum = fw_checksum + {24'd0, bval};
    end
    $display("[%0t] BOOT: Firmware loaded. %0d bytes, checksum=0x%08X",
             $time, size_bytes, fw_checksum);
end

// ---- Main driver -------------------------------------------------------------
initial begin : driver
    integer i, w, b;
    reg [7:0] rx_byte;
    reg [7:0] flash_info [0:15];

    serial_rx = 1;
    repeat (20) @(posedge clk);
    rst = 0;
    $display("[%0t] BOOT: Reset released.", $time);

    while (portb_o !== 8'h04) @(posedge clk);
    $display("[%0t] UPLOAD: portb_o=0x04. Waiting 500us for SFDP probe...", $time);
    #500_000;
    $display("[%0t] UPLOAD: Starting UART protocol.", $time);

    $display("[%0t] UPLOAD: -> SYNC (0x5A)", $time);
    uart_send_byte(8'h5A);
    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK(0x79) after SYNC, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (handshake OK)", $time);

    $display("[%0t] UPLOAD: Reading 16-byte flash info...", $time);
    for (i = 0; i < 16; i = i + 1)
        uart_recv_byte(flash_info[i]);

    $display("[%0t] UPLOAD: -> Size=%0d, Mode=MODE_RAM", $time, size_bytes);
    uart_send_byte(size_bytes[7:0]);
    uart_send_byte(size_bytes[15:8]);
    uart_send_byte(size_bytes[23:16]);
    uart_send_byte(size_bytes[31:24]);
    uart_send_byte(8'h01);

    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK after size/mode, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (size/mode OK)", $time);

    $display("[%0t] UPLOAD: Streaming %0d bytes...", $time, nbytes);
    for (i = 0; i < nbytes; i = i + 1) begin
        w = i >> 2; b = i & 3;
        uart_send_byte(pay[1 + w][b*8 +: 8]);
    end
    $display("[%0t] UPLOAD: Payload sent.", $time);

    $display("[%0t] UPLOAD: -> Checksum=0x%08X", $time, fw_checksum);
    uart_send_byte(fw_checksum[7:0]);
    uart_send_byte(fw_checksum[15:8]);
    uart_send_byte(fw_checksum[23:16]);
    uart_send_byte(fw_checksum[31:24]);

    uart_recv_byte(rx_byte);
    if (rx_byte === 8'h79)
        $display("[%0t] UPLOAD: <- ACK (firmware accepted). Running...", $time);
    else if (rx_byte === 8'h1F)
        $display("[%0t] FAIL: <- NAK (checksum mismatch)", $time);
    else
        $display("[%0t] FAIL: Unexpected final byte 0x%02X", $time, rx_byte);
end

endmodule
