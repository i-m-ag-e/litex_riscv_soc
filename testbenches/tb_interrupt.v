// =============================================================================
// tb_interrupt.v — picorv32 interrupt-path bring-up test (UART-UPLOAD variant)
// =============================================================================
// Target:    asic_top_fixed.v  (flattened PicoRV32 SoC)
// Simulator: Verilator --timing  (also XSim-compatible)
// Firmware:  firmware/tests/test_irq_sim.c
//
// Firmware delivery: UPLOADED over UART0 (prog_mode=1, bootloader State C), NOT
// pre-loaded into main_ram. The SPRAM main_ram macro cannot be pre-initialized
// via $readmemh, so warm-boot pre-load is impossible on the ASIC netlist — the
// firmware must be streamed in at runtime exactly as on real silicon.
//   * main_ram starts as all zeros  (gen_init.py --mode upload)
//   * the payload is streamed from irq_payload.init (self-describing: word 0 =
//     firmware byte count, then firmware words)
//
// No loopback / external stimulus needed: once running, the firmware enables the
// timer0 periodic interrupt and waits for the CPU ISR. Reaching 0xAA proves the
// whole interrupt path (enable, mask, PROGADDR_IRQ vector, ISR execution,
// return-from-interrupt). A broken IRQ path never reaches 0xAA -> watchdog.
//
// Pass:  portb_o = 0xAA   (ISR ran)
// Fail:  watchdog timeout  (ISR never ran / never returned)
//
// Baud: 115200 (868 clk/bit @ 100 MHz = 8680 ns/bit)
// =============================================================================

`timescale 1ns / 1ps

module tb_interrupt;

localparam integer CLKS_PER_BIT  = 868;
localparam integer BIT_PERIOD_NS = CLKS_PER_BIT * 10;   // 8680 ns
localparam integer RAM_WORDS     = 8192;

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
// DUT — prog_mode=1 (State C UART upload). No peripheral ties needed.
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
        $display("[%0t] IRQ portb_o = 0x%02X", $time, portb_o);
        portb_prev <= portb_o;
    end
end

// -----------------------------------------------------------------------------
// PASS detector
// -----------------------------------------------------------------------------
always @(posedge clk) begin
    if (portb_o === 8'hAA) begin
        $display("[%0t] PASS: portb_o = 0xAA — timer0 interrupt reached the CPU ISR (uploaded fw).", $time);
        #100; $finish;
    end
end

// -----------------------------------------------------------------------------
// Watchdog — upload (~105 ms) + firmware run. 800 ms envelope.
// -----------------------------------------------------------------------------
initial begin
    #800_000_000;
    $display("[%0t] TIMEOUT FAIL: portb_o never reached 0xAA — IRQ path did not fire.", $time);
    $finish;
end

// =============================================================================
// UART firmware-upload machinery (bootloader State C, MODE_RAM) — via UART0
// =============================================================================

// ---- DUT-TX receive FIFO -----------------------------------------------------
// The always @(negedge serial_tx) monitor is ALWAYS ready — it can never miss a
// start bit the way a task called after send_byte can. It decodes each byte into
// a small circular FIFO; uart_recv_byte() reads from there.
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
reg [31:0] size_bytes;               // exact byte count (for LE byte extraction)
integer    nbytes;                   // signed copy for loop arithmetic
reg [31:0] fw_checksum;

initial begin : load_fw
    integer i, w, b;
    reg [7:0] bval;
    $readmemh("irq_payload.init", pay);
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

    // Wait for State C entry (portb_o=0x04), then 500 us for the bootloader to
    // finish flash_read_sfdp() before it drains UART RX.
    while (portb_o !== 8'h04) @(posedge clk);
    $display("[%0t] UPLOAD: portb_o=0x04. Waiting 500us for SFDP probe...", $time);
    #500_000;
    $display("[%0t] UPLOAD: Starting UART protocol.", $time);

    // SYNC -> ACK
    $display("[%0t] UPLOAD: -> SYNC (0x5A)", $time);
    uart_send_byte(8'h5A);
    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK(0x79) after SYNC, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (handshake OK)", $time);

    // 16-byte flash info block
    $display("[%0t] UPLOAD: Reading 16-byte flash info...", $time);
    for (i = 0; i < 16; i = i + 1)
        uart_recv_byte(flash_info[i]);

    // Size (4 bytes LE) + Mode = MODE_RAM (0x01)
    $display("[%0t] UPLOAD: -> Size=%0d, Mode=MODE_RAM", $time, size_bytes);
    uart_send_byte(size_bytes[7:0]);
    uart_send_byte(size_bytes[15:8]);
    uart_send_byte(size_bytes[23:16]);
    uart_send_byte(size_bytes[31:24]);
    uart_send_byte(8'h01);

    // ACK for size/mode
    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK after size/mode, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (size/mode OK)", $time);

    // Payload
    $display("[%0t] UPLOAD: Streaming %0d bytes...", $time, nbytes);
    for (i = 0; i < nbytes; i = i + 1) begin
        w = i >> 2; b = i & 3;
        uart_send_byte(pay[1 + w][b*8 +: 8]);
    end
    $display("[%0t] UPLOAD: Payload sent.", $time);

    // Checksum (4 bytes LE)
    $display("[%0t] UPLOAD: -> Checksum=0x%08X", $time, fw_checksum);
    uart_send_byte(fw_checksum[7:0]);
    uart_send_byte(fw_checksum[15:8]);
    uart_send_byte(fw_checksum[23:16]);
    uart_send_byte(fw_checksum[31:24]);

    // Final ACK/NAK
    uart_recv_byte(rx_byte);
    if (rx_byte === 8'h79)
        $display("[%0t] UPLOAD: <- ACK (firmware accepted). Running...", $time);
    else if (rx_byte === 8'h1F)
        $display("[%0t] FAIL: <- NAK (checksum mismatch)", $time);
    else
        $display("[%0t] FAIL: Unexpected final byte 0x%02X", $time, rx_byte);
end

endmodule
