`timescale 1ns/1ps

module top (
    // Clock and Reset
    inout  wire       clk,
    inout  wire       rst,
    
    // I2C
    inout  wire       i2c_scl,
    inout  wire       i2c_sda,
    
    // GPIO Ports
    inout  wire [7:0] portb,
    inout  wire [7:0] portc,
    inout  wire [7:0] portd,
    
    // Programming and PWM
    inout  wire       prog_mode,
    inout wire       pwm_ch_a,
    inout wire       pwm_ch_b,
    inout wire       pwm_ch_c,
    inout wire       pwm_ch_d,
    
    // Serial / UART
    inout  wire       serial_rx,
    inout wire       serial_tx,
    inout  wire       uart1_rx,
    inout wire       uart1_tx,
    
    // SPI
    inout wire       spi_clk,
    inout wire       spi_cs_n,
    inout  wire       spi_miso,
    inout wire       spi_mosi,
    
    // SPI Flash
    inout wire       spiflash_clk,
    inout wire       spiflash_cs_n,
    inout  wire       spiflash_miso,
    inout wire       spiflash_mosi
);

    // =========================================================================
    // Power OK Signals
    // =========================================================================
    wire vdd_ok;
    wire vddo_ok;

    POK_18_18 pok_inst (
        .VDDO_OK (vddo_ok), 
        .VDD_OK  (vdd_ok)
    );

    // =========================================================================
    // Internal Wires connecting Pads to Core
    // =========================================================================
    
    // Core inputs (from PAD CIN)
    wire       core_clk_i;
    wire       core_rst_i;
    wire       core_i2c_scl_i;
    wire       core_i2c_sda_i;
    wire [7:0] core_portb_i;
    wire [7:0] core_portc_i;
    wire [7:0] core_portd_i;
    wire       core_prog_mode_i;
    wire       core_serial_rx_i;
    wire       core_spi_miso_i;
    wire       core_spiflash_miso_i;
    wire       core_uart1_rx_i;

    // Core outputs (to PAD I)
    wire       core_i2c_scl_o;
    wire       core_i2c_sda_o;
    wire [7:0] core_portb_o;
    wire [7:0] core_portc_o;
    wire [7:0] core_portd_o;
    wire       core_pwm_ch_a_o;
    wire       core_pwm_ch_b_o;
    wire       core_pwm_ch_c_o;
    wire       core_pwm_ch_d_o;
    wire       core_serial_tx_o;
    wire       core_spi_clk_o;
    wire       core_spi_cs_n_o;
    wire       core_spi_mosi_o;
    wire       core_spiflash_clk_o;
    wire       core_spiflash_cs_n_o;
    wire       core_spiflash_mosi_o;
    wire       core_uart1_tx_o;

    // Core Output Enables (to PAD EN)
    wire       core_i2c_scl_oe;
    wire       core_i2c_sda_oe;
    wire [7:0] core_portb_oe;
    wire [7:0] core_portc_oe;
    wire [7:0] core_portd_oe;

    // =========================================================================
    // Pad Instantiations
    // =========================================================================

    // MACRO: 
    // SCHM_ON = 1 for clk/rst, 0 otherwise
    // PULL_ON / PULL_UP_DOWN = 1 for I2C, 0 otherwise
    // DRIVE_STR = 1
    // TTL_ON = 0
    // IE = ~EN

    // --------------------------------------------------
    // Clock & Reset Pads (Inputs with Schmitt Triggers)
    // --------------------------------------------------
    PIOH_18_18 pad_clk (
        .PAD(clk), .CIN(core_clk_i), .I(1'b0), .EN(1'b0), .IE(1'b1),
        .SCHM_ON(1'b1), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
        .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
    );

    PIOH_18_18 pad_rst (
        .PAD(rst), .CIN(core_rst_i), .I(1'b0), .EN(1'b0), .IE(1'b1),
        .SCHM_ON(1'b1), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
        .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
    );

    // --------------------------------------------------
    // I2C Pads (Inouts with Pull-ups)
    // --------------------------------------------------
    PIOH_18_18 pad_i2c_scl (
        .PAD(i2c_scl), .CIN(core_i2c_scl_i), .I(core_i2c_scl_o), .EN(core_i2c_scl_oe), .IE(~core_i2c_scl_oe),
        .SCHM_ON(1'b0), .PULL_ON(1'b1), .PULL_UP_DOWN(1'b1), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
        .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
    );

    PIOH_18_18 pad_i2c_sda (
        .PAD(i2c_sda), .CIN(core_i2c_sda_i), .I(core_i2c_sda_o), .EN(core_i2c_sda_oe), .IE(~core_i2c_sda_oe),
        .SCHM_ON(1'b0), .PULL_ON(1'b1), .PULL_UP_DOWN(1'b1), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
        .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
    );

    // --------------------------------------------------
    // GPIO Vectors (Inouts)
    // --------------------------------------------------
    genvar i;
    generate
        for (i = 0; i < 8; i = i + 1) begin : gen_portb
            PIOH_18_18 pad_portb (
                .PAD(portb[i]), .CIN(core_portb_i[i]), .I(core_portb_o[i]), .EN(core_portb_oe[i]), .IE(~core_portb_oe[i]),
                .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
                .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
            );
        end

        for (i = 0; i < 8; i = i + 1) begin : gen_portc
            PIOH_18_18 pad_portc (
                .PAD(portc[i]), .CIN(core_portc_i[i]), .I(core_portc_o[i]), .EN(core_portc_oe[i]), .IE(~core_portc_oe[i]),
                .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
                .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
            );
        end

        for (i = 0; i < 8; i = i + 1) begin : gen_portd
            PIOH_18_18 pad_portd (
                .PAD(portd[i]), .CIN(core_portd_i[i]), .I(core_portd_o[i]), .EN(core_portd_oe[i]), .IE(~core_portd_oe[i]),
                .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0),
                .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok)
            );
        end
    endgenerate

    // --------------------------------------------------
    // Standard Input Pads (EN=0, IE=1)
    // --------------------------------------------------
    PIOH_18_18 pad_prog_mode     ( .PAD(prog_mode),     .CIN(core_prog_mode_i),     .I(1'b0), .EN(1'b0), .IE(1'b1), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_serial_rx     ( .PAD(serial_rx),     .CIN(core_serial_rx_i),     .I(1'b0), .EN(1'b0), .IE(1'b1), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spi_miso      ( .PAD(spi_miso),      .CIN(core_spi_miso_i),      .I(1'b0), .EN(1'b0), .IE(1'b1), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spiflash_miso ( .PAD(spiflash_miso), .CIN(core_spiflash_miso_i), .I(1'b0), .EN(1'b0), .IE(1'b1), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_uart1_rx      ( .PAD(uart1_rx),      .CIN(core_uart1_rx_i),      .I(1'b0), .EN(1'b0), .IE(1'b1), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );

    // --------------------------------------------------
    // Standard Output Pads (EN=1, IE=0)
    // --------------------------------------------------
    PIOH_18_18 pad_pwm_ch_a      ( .PAD(pwm_ch_a),      .CIN(), .I(core_pwm_ch_a_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_pwm_ch_b      ( .PAD(pwm_ch_b),      .CIN(), .I(core_pwm_ch_b_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_pwm_ch_c      ( .PAD(pwm_ch_c),      .CIN(), .I(core_pwm_ch_c_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_pwm_ch_d      ( .PAD(pwm_ch_d),      .CIN(), .I(core_pwm_ch_d_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_serial_tx     ( .PAD(serial_tx),     .CIN(), .I(core_serial_tx_o),     .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spi_clk       ( .PAD(spi_clk),       .CIN(), .I(core_spi_clk_o),       .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spi_cs_n      ( .PAD(spi_cs_n),      .CIN(), .I(core_spi_cs_n_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spi_mosi      ( .PAD(spi_mosi),      .CIN(), .I(core_spi_mosi_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spiflash_clk  ( .PAD(spiflash_clk),  .CIN(), .I(core_spiflash_clk_o),  .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spiflash_cs_n ( .PAD(spiflash_cs_n), .CIN(), .I(core_spiflash_cs_n_o), .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_spiflash_mosi ( .PAD(spiflash_mosi), .CIN(), .I(core_spiflash_mosi_o), .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );
    PIOH_18_18 pad_uart1_tx      ( .PAD(uart1_tx),      .CIN(), .I(core_uart1_tx_o),      .EN(1'b1), .IE(1'b0), .SCHM_ON(1'b0), .PULL_ON(1'b0), .PULL_UP_DOWN(1'b0), .DRIVE_STR(1'b1), .TTL_ON(1'b0), .VDD_OK(vdd_ok), .VDDO_OK(vddo_ok) );

    // =========================================================================
    // Core Instantiation
    // =========================================================================
    core u_core (
        .clk           (core_clk_i),
        .i2c_scl_i     (core_i2c_scl_i),
        .i2c_scl_o     (core_i2c_scl_o),
        .i2c_scl_oe    (core_i2c_scl_oe),
        .i2c_sda_i     (core_i2c_sda_i),
        .i2c_sda_o     (core_i2c_sda_o),
        .i2c_sda_oe    (core_i2c_sda_oe),
        .portb_i       (core_portb_i),
        .portb_o       (core_portb_o),
        .portb_oe      (core_portb_oe),
        .portc_i       (core_portc_i),
        .portc_o       (core_portc_o),
        .portc_oe      (core_portc_oe),
        .portd_i       (core_portd_i),
        .portd_o       (core_portd_o),
        .portd_oe      (core_portd_oe),
        .prog_mode     (core_prog_mode_i),
        .pwm_ch_a      (core_pwm_ch_a_o),
        .pwm_ch_b      (core_pwm_ch_b_o),
        .pwm_ch_c      (core_pwm_ch_c_o),
        .pwm_ch_d      (core_pwm_ch_d_o),
        .rst           (core_rst_i),
        .serial_rx     (core_serial_rx_i),
        .serial_tx     (core_serial_tx_o),
        .spi_clk       (core_spi_clk_o),
        .spi_cs_n      (core_spi_cs_n_o),
        .spi_miso      (core_spi_miso_i),
        .spi_mosi      (core_spi_mosi_o),
        .spiflash_clk  (core_spiflash_clk_o),
        .spiflash_cs_n (core_spiflash_cs_n_o),
        .spiflash_miso (core_spiflash_miso_i),
        .spiflash_mosi (core_spiflash_mosi_o),
        .uart1_rx      (core_uart1_rx_i),
        .uart1_tx      (core_uart1_tx_o)
    );

endmodule
