// =============================================================================
// tb_i2c.v — I2C-master bring-up test (UART-UPLOAD variant)
// =============================================================================
// Target:    asic_top_fixed.v  (flattened PicoRV32 SoC)
// Simulator: Verilator --timing  (also XSim-compatible)
// Firmware:  firmware/tests/test_i2c_sim.c
//
// Firmware delivery: UPLOADED over UART0 (prog_mode=1, bootloader State C), NOT
// pre-loaded into main_ram (the SPRAM main_ram macro has no $readmemh).
//   * main_ram starts all zeros  (gen_init.py --mode upload)
//   * payload streamed from i2c_payload.init (word 0 = byte count, then firmware)
//
// The SoC is an I2C MASTER. This bench models the open-drain I2C bus (wired-AND
// with pull-ups) and instantiates an I2C SLAVE (i2c_slave_model, addr 0x42) that
// ACKs its address, stores the byte written to it, and returns that byte on
// reads. The firmware writes-then-reads (loopback through the slave) and checks a
// wrong-address access NACKs.
//
// Pass:  portb_o = 0xAA   (write/read/NACK all as expected)
// Fail:  portb_o = 0xF0   (bad readback or wrong ACK/NACK)  OR  watchdog timeout
//
// Baud: 115200 (868 clk/bit @ 100 MHz = 8680 ns/bit)
// =============================================================================

`timescale 1ns / 1ps

module tb_i2c;

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
// Open-drain I2C bus model (pull-ups = 1; any driver pulling low wins).
//   * Master (DUT) pulls SCL low when scl_oe & ~scl_o  (scl_o is constant 0).
//   * Master pulls SDA low when sda_oe & ~sda_o; it releases (sda_oe=0) during
//     ACK and read-data phases so the slave can drive.
//   * Slave pulls SDA low when slv_sda_low. It never stretches SCL.
// -----------------------------------------------------------------------------
wire slv_sda_low;
wire scl_bus = ~(i2c_scl_oe & ~i2c_scl_o);                 // master-only SCL
wire sda_bus = ~((i2c_sda_oe & ~i2c_sda_o) | slv_sda_low); // wired-AND SDA

i2c_slave_model #(.ADDR(7'h42)) slave (
    .scl    (scl_bus),
    .sda_in (sda_bus),
    .sda_low(slv_sda_low)
);

// -----------------------------------------------------------------------------
// DUT — prog_mode=1 (State C UART upload). I2C bus wired to the slave model.
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
    .i2c_scl_i(scl_bus), .i2c_scl_o(i2c_scl_o), .i2c_scl_oe(i2c_scl_oe),
    .i2c_sda_i(sda_bus), .i2c_sda_o(i2c_sda_o), .i2c_sda_oe(i2c_sda_oe),
    .pwm_ch_a(pwm_ch_a), .pwm_ch_b(pwm_ch_b), .pwm_ch_c(pwm_ch_c), .pwm_ch_d(pwm_ch_d)
);

// -----------------------------------------------------------------------------
// portb_o monitor
// -----------------------------------------------------------------------------
reg [7:0] portb_prev = 8'hFF;
always @(posedge clk) begin
    if (portb_o !== portb_prev) begin
        $display("[%0t] I2C portb_o = 0x%02X", $time, portb_o);
        portb_prev <= portb_o;
    end
end

// -----------------------------------------------------------------------------
// PASS / FAIL detectors
// -----------------------------------------------------------------------------
always @(posedge clk) begin
    if (portb_o === 8'hAA) begin
        $display("[%0t] PASS: portb_o = 0xAA — I2C write/read/NACK verified (uploaded fw ran).", $time);
        #100; $finish;
    end
    if (portb_o === 8'hF0) begin
        $display("[%0t] FAIL: portb_o = 0xF0 — I2C transaction mismatch.", $time);
        #100; $finish;
    end
end

// -----------------------------------------------------------------------------
// Watchdog — upload (~105 ms) + firmware run. 800 ms envelope.
// -----------------------------------------------------------------------------
initial begin
    #800_000_000;
    $display("[%0t] TIMEOUT FAIL: no 0xAA/0xF0 on portb_o within 800 ms sim.", $time);
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
    $readmemh("i2c_payload.init", pay);
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


// =============================================================================
// i2c_slave_model — minimal single-byte-register I2C slave
// =============================================================================
// Behaviour:
//   * Matches 7-bit ADDR. Non-matching address -> no ACK (master sees NACK).
//   * Write transaction: stores the received data byte into an internal reg.
//   * Read transaction: drives that stored byte back (MSB first).
//
// Standard I2C wire protocol: data set up while SCL low, sampled while SCL high;
// START = SDA falling while SCL high, STOP = SDA rising while SCL high. Single
// edge-detecting process (one driver for all state -> Verilator-clean). sda_low=1
// means "pull SDA low"; releasing (sda_low=0) lets the pull-up give a 1.
// =============================================================================
module i2c_slave_model #(
    parameter [6:0] ADDR = 7'h42
) (
    input  wire scl,
    input  wire sda_in,
    output reg  sda_low
);
    localparam ST_IDLE     = 0;
    localparam ST_ADDR     = 1;
    localparam ST_ACK_ADDR = 2;
    localparam ST_WRITE    = 3;
    localparam ST_ACK_WR   = 4;
    localparam ST_READ     = 5;
    localparam ST_ACK_RD   = 6;

    reg [2:0] state    = ST_IDLE;
    reg [3:0] cnt      = 0;          // bits sampled/driven in current byte
    reg [7:0] sh       = 0;          // RX shift register
    reg [7:0] tx_sh    = 0;          // TX shift register (read data)
    reg [7:0] slave_reg = 8'h00;     // the stored register (write stores, read returns)
    reg       rw       = 0;          // captured R/W bit (1 = read)
    reg       ack_ph   = 0;          // 0 = drive ACK, 1 = release/advance
    reg       prev_scl = 1;
    reg       prev_sda = 1;

    wire [7:0] full = {sh[6:0], sda_in};   // byte-in-progress incl. current bit

    always @(scl or sda_in) begin
        // ---- START / STOP (SDA edge while SCL high) --------------------------
        if (prev_scl && scl && prev_sda && !sda_in) begin          // START
            state   <= ST_ADDR;
            cnt     <= 0;
            sda_low <= 1'b0;
        end
        else if (prev_scl && scl && !prev_sda && sda_in) begin     // STOP
            state   <= ST_IDLE;
            sda_low <= 1'b0;
        end
        // ---- SCL rising edge: sample master data -----------------------------
        else if (!prev_scl && scl) begin
            case (state)
                ST_ADDR: begin
                    sh <= full;
                    if (cnt == 7) begin
                        cnt <= 0;
                        if (full[7:1] == ADDR) begin
                            rw     <= full[0];
                            ack_ph <= 1'b0;
                            state  <= ST_ACK_ADDR;
                        end else begin
                            state <= ST_IDLE;          // address miss -> NACK
                        end
                    end else begin
                        cnt <= cnt + 1;
                    end
                end
                ST_WRITE: begin
                    sh <= full;
                    if (cnt == 7) begin
                        cnt    <= 0;
                        ack_ph <= 1'b0;
                        state  <= ST_ACK_WR;
                    end else begin
                        cnt <= cnt + 1;
                    end
                end
                // ST_READ / ST_ACK_* : master samples/drives; slave does not sample
                default: ;
            endcase
        end
        // ---- SCL falling edge: set up what the slave drives next --------------
        else if (prev_scl && !scl) begin
            case (state)
                ST_ACK_ADDR: begin
                    if (!ack_ph) begin
                        sda_low <= 1'b1;               // drive ACK low
                        ack_ph  <= 1'b1;
                    end else begin
                        ack_ph <= 1'b0;
                        if (rw) begin                  // read: start driving data
                            tx_sh   <= slave_reg;
                            sda_low <= ~slave_reg[7];  // MSB first
                            cnt     <= 1;
                            state   <= ST_READ;
                        end else begin                 // write: receive data byte
                            sda_low <= 1'b0;
                            cnt     <= 0;
                            state   <= ST_WRITE;
                        end
                    end
                end
                ST_READ: begin
                    if (cnt < 8) begin
                        sda_low <= ~tx_sh[7 - cnt];    // next data bit (MSB first)
                        cnt     <= cnt + 1;
                    end else begin
                        sda_low <= 1'b0;               // release for master ACK/NACK
                        state   <= ST_ACK_RD;
                    end
                end
                ST_ACK_WR: begin
                    if (!ack_ph) begin
                        sda_low   <= 1'b1;             // ACK the written byte
                        slave_reg <= sh;               // store it
                        ack_ph    <= 1'b1;
                    end else begin
                        sda_low <= 1'b0;               // release; await STOP
                        state   <= ST_IDLE;
                    end
                end
                ST_ACK_RD: begin
                    sda_low <= 1'b0;                   // released; await STOP
                    state   <= ST_IDLE;
                end
                default: ;
            endcase
        end

        prev_scl <= scl;
        prev_sda <= sda_in;
    end

endmodule
