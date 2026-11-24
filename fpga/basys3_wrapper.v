`timescale 1ns / 1ps

module basys3_top (
    // 100MHz Clock from onboard oscillator
    input  wire       clk100,
    
    // Reset button
    input  wire       btnC,
    
    // Main UART
    input  wire       RsRx,
    output wire       RsTx,
    
    // Secondary UART
    input  wire       uart1_rx,
    output wire       uart1_tx,
    
    // PWM Channels
    output wire       pwm_ch_a,
    output wire       pwm_ch_b,
    output wire       pwm_ch_c,
    output wire       pwm_ch_d,
    
    // SPI
    output wire       spi_clk,
    output wire       spi_cs_n,
    input  wire       spi_miso,
    output wire       spi_mosi,
    
    // I2C - Physical inout ports
    inout  wire       i2c_scl,
    inout  wire       i2c_sda,
    
    // Dedicated SPI Flash
    output wire       spiflash_cs_n,
    input  wire       spiflash_miso,
    output wire       spiflash_mosi,
    
    // GPIO Ports - Physical inout ports
    inout  wire [7:0] portb_pin,
    inout  wire [7:0] portc_pin,
    inout  wire [7:0] portd_pin
);

    //--------------------------------------------------------------------------
    // 1. Clock Generation (100 MHz -> 50 MHz)
    //--------------------------------------------------------------------------
    wire clkfb;
    wire sys_clk_unbuf;
    wire sys_clk;

    MMCME2_BASE #(
        .BANDWIDTH("OPTIMIZED"),
        .CLKFBOUT_MULT_F(10.0),    // VCO = 1000 MHz
        .CLKFBOUT_PHASE(0.0),
        .CLKIN1_PERIOD(10.0),      // 100 MHz input
        .CLKOUT0_DIVIDE_F(20.0),   // 1000 / 20 = 50 MHz output
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT0_PHASE(0.0)
    ) mmcm_inst (
        .CLKIN1(clk100),
        .CLKOUT0(sys_clk_unbuf),
        .CLKFBOUT(clkfb),
        .CLKFBIN(clkfb),
        .PWRDWN(1'b0),
        .RST(btnC)
    );

    BUFG bufg_sys (.I(sys_clk_unbuf), .O(sys_clk));

    //--------------------------------------------------------------------------
    // 2. SPI Flash Clock Routing (STARTUPE2)
    //--------------------------------------------------------------------------
    wire internal_spiflash_clk;
    
    STARTUPE2 #(
        .PROG_USR("FALSE"),
        .SIM_CCLK_FREQ(0.0)
    ) startupe2_inst (
        .CFGCLK(),
        .CFGMCLK(),
        .EOS(),
        .PREQ(),
        .CLK(1'b0),
        .GSR(1'b0),
        .GTS(1'b0),
        .KEYCLEARB(1'b1),
        .PACK(1'b0),
        .USRCCLKO(internal_spiflash_clk),
        .USRCCLKTS(1'b0),
        .USRDONEO(1'b1),
        .USRDONETS(1'b0)
    );

    //--------------------------------------------------------------------------
    // 3. Xilinx IOBUF Instantiations (Bidirectional pad logic)
    //--------------------------------------------------------------------------
    wire i2c_sda_i, i2c_sda_o, i2c_sda_oe;
    wire i2c_scl_i, i2c_scl_o, i2c_scl_oe;
    
    // I2C SDA Buffer
    IOBUF iobuf_sda (
        .O(i2c_sda_i),    // Buffer output (to SoC)
        .IO(i2c_sda),     // Top-level bidirectional pin
        .I(i2c_sda_o),    // Buffer input (from SoC)
        .T(~i2c_sda_oe)   // Tristate enable (Active-high input mode, so invert _oe)
    );

    // I2C SCL Buffer
    IOBUF iobuf_scl (
        .O(i2c_scl_i),
        .IO(i2c_scl),
        .I(i2c_scl_o),
        .T(~i2c_scl_oe)
    );

    // Port B Buffers
    wire [7:0] portb_i, portb_o, portb_oe;
    genvar b;
    generate
        for (b = 0; b < 8; b = b + 1) begin : gen_portb_iobuf
            IOBUF iobuf_portb (
                .O(portb_i[b]),
                .IO(portb_pin[b]),
                .I(portb_o[b]),
                .T(~portb_oe[b])
            );
        end
    endgenerate

    // Port C Buffers
    wire [7:0] portc_i, portc_o, portc_oe;
    genvar c;
    generate
        for (c = 0; c < 8; c = c + 1) begin : gen_portc_iobuf
            IOBUF iobuf_portc (
                .O(portc_i[c]),
                .IO(portc_pin[c]),
                .I(portc_o[c]),
                .T(~portc_oe[c])
            );
        end
    endgenerate

    // Port D Buffers
    wire [7:0] portd_i, portd_o, portd_oe;
    genvar d;
    generate
        for (d = 0; d < 8; d = d + 1) begin : gen_portd_iobuf
            IOBUF iobuf_portd (
                .O(portd_i[d]),
                .IO(portd_pin[d]),
                .I(portd_o[d]),
                .T(~portd_oe[d])
            );
        end
    endgenerate

    //--------------------------------------------------------------------------
    // 4. Instantiate the generated SoC (top.v)
    //--------------------------------------------------------------------------
    top asic_soc_inst (
        .clk            (sys_clk),
        .rst            (btnC),
        
        .serial_rx      (RsRx),
        .serial_tx      (RsTx),
        
        .uart1_rx       (uart1_rx),
        .uart1_tx       (uart1_tx),
        
        .pwm_ch_a       (pwm_ch_a),
        .pwm_ch_b       (pwm_ch_b),
        .pwm_ch_c       (pwm_ch_c),
        .pwm_ch_d       (pwm_ch_d),
        
        .spi_clk        (spi_clk),
        .spi_cs_n       (spi_cs_n),
        .spi_miso       (spi_miso),
        .spi_mosi       (spi_mosi),
        
        .spiflash_clk   (internal_spiflash_clk),
        .spiflash_cs_n  (spiflash_cs_n),
        .spiflash_miso  (spiflash_miso),
        .spiflash_mosi  (spiflash_mosi),
        
        .i2c_scl_i      (i2c_scl_i),
        .i2c_scl_o      (i2c_scl_o),
        .i2c_scl_oe     (i2c_scl_oe),
        .i2c_sda_i      (i2c_sda_i),
        .i2c_sda_o      (i2c_sda_o),
        .i2c_sda_oe     (i2c_sda_oe),
        
        .portb_i        (portb_i),
        .portb_o        (portb_o),
        .portb_oe       (portb_oe),
        
        .portc_i        (portc_i),
        .portc_o        (portc_o),
        .portc_oe       (portc_oe),
        
        .portd_i        (portd_i),
        .portd_o        (portd_o),
        .portd_oe       (portd_oe)
    );

endmodule
