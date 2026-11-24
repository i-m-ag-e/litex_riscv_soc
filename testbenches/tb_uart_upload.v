// =============================================================================
// tb_uart_upload.v — State C UART Firmware Upload Test (MODE_RAM)
// =============================================================================
// Target:    asic_top_fixed.v  (flattened PicoRV32 SoC)
// Simulator: Verilator --timing --binary
//
// Pass conditions:
//   1. portb_o: 0x01 → 0x04 → 0x0F → 0x00 (bootloader protocol complete)
//   2. portb_o = 0xAA (firmware running after UART load)
//   3. Final byte from bootloader is ACK (0x79), not NAK
//
// Baud: 115200 (tuning_word=4947802, 32-bit acc, 100MHz → 868 clk/bit = 8680 ns/bit)
//
// RX architecture: the DUT-TX monitor (always @negedge serial_tx) captures every
// byte into a circular FIFO. uart_recv_byte() reads from that FIFO. This avoids
// the start-bit miss that occurs when the bootloader sends a byte WHILE the driver
// is still inside uart_send_byte() — a real scenario here, since the bootloader
// queues ACK immediately after receiving SYNC, often before send_byte returns.
// =============================================================================

`timescale 1ns / 1ps

module tb_uart_upload;

localparam integer CLKS_PER_BIT  = 868;
localparam integer BIT_PERIOD_NS = CLKS_PER_BIT * 10;   // 8680 ns

localparam integer PAYLOAD_BYTES = 1468;
localparam integer FW_WORDS      = 367;    // PAYLOAD_BYTES / 4
localparam integer RAM_WORDS     = 8192;   // full main_ram

// ---------------------------------------------------------------------------
// Clock & Reset
// ---------------------------------------------------------------------------
reg clk = 0;
reg rst = 1;
always #5 clk = ~clk;

// ---------------------------------------------------------------------------
// DUT ports
// ---------------------------------------------------------------------------
wire [7:0] portb_o, portb_oe, portc_o, portc_oe, portd_o, portd_oe;
wire serial_tx;
reg  serial_rx = 1;
wire uart1_tx;
wire spiflash_clk, spiflash_cs_n, spiflash_mosi;
wire spi_clk, spi_cs_n, spi_mosi;
wire pwm_ch_a, pwm_ch_b, pwm_ch_c, pwm_ch_d;
wire i2c_scl_o, i2c_scl_oe, i2c_sda_o, i2c_sda_oe;

// ---------------------------------------------------------------------------
// DUT
// ---------------------------------------------------------------------------
top dut (
    .clk(clk), .rst(rst), .prog_mode(1'b1),
    .portb_i(8'h00), .portb_o(portb_o), .portb_oe(portb_oe),
    .portc_i(8'h00), .portc_o(portc_o), .portc_oe(portc_oe),
    .portd_i(8'h00), .portd_o(portd_o), .portd_oe(portd_oe),
    .serial_rx(serial_rx), .serial_tx(serial_tx),
    .uart1_rx(1'b1), .uart1_tx(uart1_tx),
    .spi_miso(1'b0), .spi_clk(spi_clk), .spi_cs_n(spi_cs_n), .spi_mosi(spi_mosi),
    .spiflash_miso(1'b0), .spiflash_clk(spiflash_clk),
    .spiflash_cs_n(spiflash_cs_n), .spiflash_mosi(spiflash_mosi),
    .i2c_scl_i(1'b1), .i2c_scl_o(i2c_scl_o), .i2c_scl_oe(i2c_scl_oe),
    .i2c_sda_i(1'b1), .i2c_sda_o(i2c_sda_o), .i2c_sda_oe(i2c_sda_oe),
    .pwm_ch_a(pwm_ch_a), .pwm_ch_b(pwm_ch_b),
    .pwm_ch_c(pwm_ch_c), .pwm_ch_d(pwm_ch_d)
);

// ---------------------------------------------------------------------------
// portb_o monitor
// ---------------------------------------------------------------------------
reg [7:0] portb_prev = 8'hFF;
always @(posedge clk) begin
    if (portb_o !== portb_prev) begin
        $display("[%0t] UPLOAD portb_o = 0x%02X", $time, portb_o);
        portb_prev <= portb_o;
    end
end

// ---------------------------------------------------------------------------
// PASS detector
// ---------------------------------------------------------------------------
always @(posedge clk) begin
    if (portb_o === 8'hAA) begin
        $display("[%0t] PASS: portb_o = 0xAA — UART-loaded firmware is executing!", $time);
        #100; $finish;
    end
end

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
initial begin
    #800_000_000;
    $display("[%0t] TIMEOUT FAIL: portb_o never reached 0xAA within 800 ms sim.", $time);
    $finish;
end

// ---------------------------------------------------------------------------
// SPI flash monitor
// ---------------------------------------------------------------------------
always @(negedge spiflash_cs_n)
    $display("[%0t] SPI-FLASH cs_n asserted", $time);
always @(posedge spiflash_cs_n)
    $display("[%0t] SPI-FLASH cs_n released", $time);

// ---------------------------------------------------------------------------
// DUT-TX receive FIFO
// The always @(negedge serial_tx) monitor is ALWAYS ready — it can never miss
// a start bit the way a task called after send_byte can. It decodes each byte
// and puts it in a small circular FIFO. uart_recv_byte() reads from there.
// ---------------------------------------------------------------------------
localparam RX_FIFO_DEPTH = 32;
reg [7:0] rx_fifo [0:RX_FIFO_DEPTH-1];
reg [4:0] rx_wr = 0;
reg [4:0] rx_rd = 0;

reg       rx_mon_busy = 0;
reg [7:0] rx_mon_byte;

always @(negedge serial_tx) begin
    if (!rx_mon_busy) begin
        rx_mon_busy = 1;
        #(BIT_PERIOD_NS / 2);        // advance to mid-start (still 0)
        rx_mon_byte = 8'h00;
        begin : rx_bits
            integer si;
            for (si = 0; si < 8; si = si + 1) begin
                #(BIT_PERIOD_NS);
                rx_mon_byte[si] = serial_tx;
            end
        end
        // Store in FIFO and log
        rx_fifo[rx_wr[3:0]] = rx_mon_byte;
        rx_wr = rx_wr + 1;
        $display("[%0t] DUT-TX byte = 0x%02X", $time, rx_mon_byte);
        #(BIT_PERIOD_NS);            // stop bit period
        rx_mon_busy = 0;
    end
end

// uart_recv_byte: poll FIFO until a byte is available
task uart_recv_byte;
    output [7:0] data;
    begin
        while (rx_wr == rx_rd) @(posedge clk);  // wait for FIFO entry
        data  = rx_fifo[rx_rd[3:0]];
        rx_rd = rx_rd + 1;
    end
endtask

// ---------------------------------------------------------------------------
// uart_send_byte: 8N1, LSB first
// ---------------------------------------------------------------------------
task uart_send_byte;
    input [7:0] data;
    integer i;
    begin
        serial_rx = 0;          // start bit
        #(BIT_PERIOD_NS);
        for (i = 0; i < 8; i = i + 1) begin
            serial_rx = data[i];
            #(BIT_PERIOD_NS);
        end
        serial_rx = 1;          // stop bit
        #(BIT_PERIOD_NS);
    end
endtask

// ---------------------------------------------------------------------------
// Firmware payload
// ---------------------------------------------------------------------------
reg [31:0] fw_words [0:RAM_WORDS-1];
reg [31:0] fw_checksum;

initial begin : load_fw
    integer w, b;
    reg [7:0] bval;
    fw_checksum = 32'd0;
    // Payload comes from a DEDICATED firmware image, NOT top_main_ram.init.
    // The DUT's main_ram is pre-loaded with zeros for this test (see gen_init.py
    // --mode upload), so portb_o==0xAA can only appear if the UART store path
    // actually landed these bytes in RAM — it is not masked by a resident copy.
    $readmemh("test_gpio_payload.init", fw_words);
    for (w = 0; w < FW_WORDS; w = w + 1)
        for (b = 0; b < 4; b = b + 1) begin
            bval = fw_words[w][b*8 +: 8];
            fw_checksum = fw_checksum + {24'd0, bval};
        end
    $display("[%0t] BOOT: Firmware loaded. %0d bytes, checksum=0x%08X",
             $time, PAYLOAD_BYTES, fw_checksum);
end

// ---------------------------------------------------------------------------
// Main driver
// ---------------------------------------------------------------------------
initial begin : driver
    integer i, w, b;
    reg [7:0] rx_byte;
    reg [7:0] flash_info [0:15];

    serial_rx = 1;
    repeat (20) @(posedge clk);
    rst = 0;
    $display("[%0t] BOOT: Reset released.", $time);

    // Wait for State C entry (portb_o = 0x04), then an additional 500 µs for
    // the bootloader to finish flash_read_sfdp() before it drains UART RX.
    while (portb_o !== 8'h04) @(posedge clk);
    $display("[%0t] UPLOAD: portb_o=0x04. Waiting 500us for SFDP probe...", $time);
    #500_000;
    $display("[%0t] UPLOAD: Starting UART protocol.", $time);

    // Step 2: SYNC
    $display("[%0t] UPLOAD: -> SYNC (0x5A)", $time);
    uart_send_byte(8'h5A);

    // Step 3: ACK from bootloader
    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK(0x79) after SYNC, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (handshake OK)", $time);

    // Step 4: 16-byte flash info block
    $display("[%0t] UPLOAD: Reading 16-byte flash info...", $time);
    for (i = 0; i < 16; i = i + 1)
        uart_recv_byte(flash_info[i]);
    $display("[%0t] UPLOAD: Flash info: sfdp_src=0x%02X erase=0x%02X JEDEC=%02X%02X%02X",
             $time, flash_info[0], flash_info[1],
             flash_info[4], flash_info[5], flash_info[6]);

    // Step 5: Size (4 bytes LE)
    $display("[%0t] UPLOAD: -> Size=%0d", $time, PAYLOAD_BYTES);
    uart_send_byte(PAYLOAD_BYTES[7:0]);
    uart_send_byte(PAYLOAD_BYTES[15:8]);
    uart_send_byte(PAYLOAD_BYTES[23:16]);
    uart_send_byte(PAYLOAD_BYTES[31:24]);

    // Step 6: Mode = MODE_RAM (0x01)
    uart_send_byte(8'h01);
    $display("[%0t] UPLOAD: -> Mode=MODE_RAM", $time);

    // Step 7: ACK for size/mode
    uart_recv_byte(rx_byte);
    if (rx_byte !== 8'h79) begin
        $display("[%0t] FAIL: Expected ACK after size/mode, got 0x%02X", $time, rx_byte);
        disable driver;
    end
    $display("[%0t] UPLOAD: <- ACK (size/mode OK)", $time);

    // Step 8: Payload
    $display("[%0t] UPLOAD: Streaming %0d bytes...", $time, PAYLOAD_BYTES);
    for (w = 0; w < FW_WORDS; w = w + 1)
        for (b = 0; b < 4; b = b + 1)
            uart_send_byte(fw_words[w][b*8 +: 8]);
    $display("[%0t] UPLOAD: Payload sent.", $time);

    // Step 9: Checksum (4 bytes LE)
    $display("[%0t] UPLOAD: -> Checksum=0x%08X", $time, fw_checksum);
    uart_send_byte(fw_checksum[7:0]);
    uart_send_byte(fw_checksum[15:8]);
    uart_send_byte(fw_checksum[23:16]);
    uart_send_byte(fw_checksum[31:24]);

    // Step 10: Final ACK/NAK
    uart_recv_byte(rx_byte);
    if (rx_byte === 8'h79) begin
        $display("[%0t] UPLOAD: <- ACK (checksum OK, firmware accepted). Waiting for run...", $time);
    end else if (rx_byte === 8'h1F) begin
        $display("[%0t] FAIL: <- NAK (checksum mismatch)", $time);
        disable driver;
    end else begin
        $display("[%0t] FAIL: Unexpected final byte 0x%02X", $time, rx_byte);
        disable driver;
    end
end

endmodule
