# RTL Changes: `asic_top.v` → `core.v`

- **Before:** `rtl/reference/asic_top.v` (raw LiteX output, md5 `b41425dc…`). Every line number in this file refers to it.
- **After:** `rtl/core.v`, plus two new files, `rtl/rom.v` and `rtl/top.v`.
- **Intermediate:** `rtl/sim/asic_top_fixed.v` is the state after the first pass (§2–§5, plus the first 44 resets in §4a), still with behavioural memories. Here the §4b signals are driven by a separate `always @(*)` tie-off block instead of the reset block.

| § | Change | Sites |
|---|---|---|
| 1 | Rename `top` → `core` | 1 |
| 2 | `<=` → `=` in combinational blocks | 1139 in 389 blocks |
| 3 | Declaration initializers removed | 753 |
| 4 | Resets added | 137 regs + 4 FIFO memories |
| 5 | Width fixes | 107 lines + 1 port |
| 6 | `default:` added to cases | 2 |
| 7 | Memories replaced | 4 |
| 8 | New files | `rom.v`, `top.v` |

---

## 1. Rename
- Line 23: `module top (` → `module core (`.
- Header comment: `Filename : top.v` → `core.v`.

## 2. Combinational `<=` → `=`
- Every `<=` inside every `always @(*)` block becomes `=`.
- Exception: `<=` used as a comparison inside `if (...)`.
- Sequential blocks are untouched.

## 3. Declaration initializers
- Every `reg [..] x = val;` becomes `reg [..] x;` (all 753; `core.v` has none left).
- The values move into the reset block (§4).

## 4. Resets
All added to the main `if (sys_rst) begin` block (line 10276). In `core.v` that block is also sorted alphabetically and uses tabs; there is no logic change from that.

**4a. Reset-less flops (44):**
```verilog
csr_bankarray_interface0_bank_bus_dat_r  <= 32'd0;   // ... same for interface1 … interface13 (14 total)
customasicsoc_customasicsoc_rx_data  <= 8'd0;
customasicsoc_customasicsoc_rx_count <= 4'd0;
customasicsoc_customasicsoc_rx_phase <= 32'd0;
customasicsoc_customasicsoc_tx_data  <= 8'd0;
customasicsoc_customasicsoc_tx_count <= 4'd0;
customasicsoc_customasicsoc_tx_phase <= 32'd0;
rx_data  <= 8'd0;   rx_count <= 4'd0;   rx_phase <= 32'd0;     // UART1
tx_data  <= 8'd0;   tx_count <= 4'd0;   tx_phase <= 32'd0;
spi_phy_sr_in_cnt        <= 6'd0;
spi_phy_sr_out_cnt       <= 6'd0;
customasicsoc_sr_in_cnt  <= 6'd0;
customasicsoc_sr_out_cnt <= 6'd0;
customasicsoc_mmap_burst_adr  <= 30'd0;
customasicsoc_mmap_byte_count <= 2'd0;
customasicsoc_master_rx_fifo_pipe_valid_source_first <= 1'd0;
customasicsoc_master_rx_fifo_pipe_valid_source_last  <= 1'd0;
customasicsoc_master_tx_fifo_pipe_valid_source_first <= 1'd0;
customasicsoc_master_tx_fifo_pipe_valid_source_last  <= 1'd0;
spi_master_rx_fifo_pipe_valid_source_first <= 1'd0;
spi_master_rx_fifo_pipe_valid_source_last  <= 1'd0;
spi_master_tx_fifo_pipe_valid_source_first <= 1'd0;
spi_master_tx_fifo_pipe_valid_source_last  <= 1'd0;
i2c_master_rx_fifo_pipe_valid_source_first <= 1'd0;
i2c_master_rx_fifo_pipe_valid_source_last  <= 1'd0;
i2c_master_tx_fifo_pipe_valid_source_first <= 1'd0;
i2c_master_tx_fifo_pipe_valid_source_last  <= 1'd0;
```

**4b. Never-assigned regs (37).** These are driven by nothing else, so the reset value is permanent:
```verilog
customasicsoc_customasicsoc_adr_burst               <= 1'd0;
customasicsoc_customasicsoc_interface0_ram_bus_err  <= 1'd0;
customasicsoc_customasicsoc_interface1_ram_bus_err  <= 1'd0;
customasicsoc_customasicsoc_ram_bus_err             <= 1'd0;
customasicsoc_customasicsoc_rx_source_first         <= 1'd0;
customasicsoc_customasicsoc_rx_source_last          <= 1'd0;
customasicsoc_customasicsoc_sram0_adr_burst         <= 1'd0;
customasicsoc_customasicsoc_sram1_adr_burst         <= 1'd0;
customasicsoc_customasicsoc_uart_rx_fifo_replace    <= 1'd0;
customasicsoc_customasicsoc_uart_tx_fifo_replace    <= 1'd0;
customasicsoc_customasicsoc_uart_tx_fifo_sink_first <= 1'd0;
customasicsoc_customasicsoc_uart_tx_fifo_sink_last  <= 1'd0;
customasicsoc_en_int                          <= 1'd0;
customasicsoc_master_tx_fifo_sink_sink_first  <= 1'd0;
customasicsoc_master_tx_fifo_sink_sink_last   <= 1'd0;
customasicsoc_mmap_bus_err                    <= 1'd0;
customasicsoc_mmap_dummy                      <= 32'd57005;   // 0xDEAD
customasicsoc_mmap_source_first               <= 1'd0;
customasicsoc_mmap_source_last                <= 1'd0;
customasicsoc_no_read                         <= 1'd0;
customasicsoc_source_first                    <= 1'd0;
customasicsoc_source_last                     <= 1'd0;
i2c_master_tx_fifo_sink_sink_first            <= 1'd0;
i2c_phy_source_first                          <= 1'd0;
interface0_err                                <= 1'd0;
rx_source_first                               <= 1'd0;
rx_source_last                                <= 1'd0;
spi_master_tx_fifo_sink_sink_first            <= 1'd0;
spi_master_tx_fifo_sink_sink_last             <= 1'd0;
spi_phy_en_int                                <= 1'd0;
spi_phy_no_read                               <= 1'd0;
spi_phy_source_first                          <= 1'd0;
spi_phy_source_last                           <= 1'd0;
uart1_rx_fifo_replace                         <= 1'd0;
uart1_tx_fifo_replace                         <= 1'd0;
uart1_tx_fifo_sink_first                      <= 1'd0;
uart1_tx_fifo_sink_last                       <= 1'd0;
```

**4c. Input synchronizers (54):**
- `multiregimplN0 <= 1'd0;` and `multiregimplN1 <= 1'd0;` for N = 0–26.
- These have no effect: the unconditional `multiregimpl* <= <pin>` assignments after the reset block (line 10515 onwards) override them.

**4d. Unused SPI input bits (2):**
```verilog
customasicsoc_dq_i[0] <= 1'd0;
spi_phy_dq_i[0]       <= 1'd0;
```

**4e. UART FIFO memories (4).** Each one's write block and read block are merged into one `always @(posedge sys_clk)`, with this reset added at the top:

| Memory | Line | FIFO |
|---|---|---|
| `storage` | 10657 | UART0 TX |
| `storage_1` | 10678 | UART0 RX |
| `storage_2` | 10725 | UART1 TX |
| `storage_3` | 10746 | UART1 RX |

```verilog
if (sys_rst) begin : reset_storage          // reset_storage_1 / _2 / _3
    integer i;
    storage_dat0 <= 10'd0;
    storage_dat1 <= 10'd0;
    for (i = 0; i < 16; i = i + 1) storage[i] <= 10'd0;
end
```

**Not reset:** `mem_adr0` (the identifier-string read address).

## 5. Width fixes
These only make Verilog's implicit extension or truncation explicit, so behaviour is unchanged.

**5a. Zero-extend:**

| Line | New RHS |
|---|---|
| 2678 | `…uart_tx_fifo_level0 + {4'd0, …uart_tx_fifo_readable}` |
| 2710 | `…uart_rx_fifo_level0 + {3'd0, …uart_rx_fifo_readable}` |
| 7735 | `uart1_tx_fifo_level0 + {4'd0, uart1_tx_fifo_readable}` |
| 7767 | `uart1_rx_fifo_level0 + {4'd0, uart1_rx_fifo_readable}` |
| 8062 | `csr_bankarray_sram_bus_dat_r = {24'd0, csr_bankarray_dat_r}` |
| 9398 | `i2c_phy_sink_payload_data <<< (6'd32 - {2'b0, ({1'b0, i2c_phy_len_tx_capped} * 4'd8)})` |
| 9474 | `spi_phy_sr_out_cnt <= spi_phy_sink_payload_len - {2'd0, spi_phy_sink_payload_width}` |
| 9479 | `spi_phy_sr_out_cnt <= spi_phy_sr_out_cnt - {2'b00, spi_phy_last_sink_width}` |
| 9496 | `spi_phy_sr_in_cnt <= spi_phy_sr_in_cnt - {2'b00, spi_phy_last_sink_width}` |
| 9614 | `customasicsoc_sr_out_cnt <= customasicsoc_sink_payload_len - {2'b00, customasicsoc_sink_payload_width}` |
| 9619 | `customasicsoc_sr_out_cnt <= customasicsoc_sr_out_cnt - {2'b00, customasicsoc_last_sink_width}` |
| 9636 | `customasicsoc_sr_in_cnt <= customasicsoc_sr_in_cnt - {2'b00, customasicsoc_last_sink_width}` |
| 5614, 5622 | `spi_phy_sr_in_cnt == {2'b00, spi_phy_last_sink_width}` |
| 7188, 7196 | `customasicsoc_sr_in_cnt == {2'b00, customasicsoc_last_sink_width}` |

**CSR read mux (69 sites, lines 9751–10256):** every `csr_bankarray_interfaceN_bank_bus_dat_r <= field;` with a field narrower than 32 bits becomes `<= {(32-w)'d0, field};`.

**5b. Truncate:**

| Line | Change |
|---|---|
| 5585 / 5588 / 5591 | `spi_phy_dq_o = spi_phy_sr_out[30]` / `[28]` / `[24]` (was `[31:30]` / `[31:28]` / `[31:24]`) |
| 7159 / 7162 / 7165 | `customasicsoc_dq_o = customasicsoc_sr_out[30]` / `[28]` / `[24]` (same) |
| 5684 | `spi_phy_dq_oe_litespiphy_next_value1 = spi_phy_sink_payload_mask[0]` |
| 7349 | `customasicsoc_dq_oe_litespiphy_next_value4 = customasicsoc_sink_payload_mask[0]` |
| 6105 | `spi_master_tx_fifo_sink_sink_payload_len = spi_master_csrfield_len[5:0]` |
| 6690 | `customasicsoc_mmap_source_payload_len = customasicsoc_mmap_spi_dummy_bits[5:0]` |
| 7094 | `customasicsoc_master_tx_fifo_sink_sink_payload_len = customasicsoc_master_len[5:0]` |
| 7805 | `interface1_adr = interface0_adr[13:0]` |
| 9484 | `spi_phy_sr_in <= {spi_phy_sr_in[30:0], spi_phy_dq_i[1]}` |
| 9487, 9490, 9493 | `spi_phy_sr_in <= {spi_phy_sr_in[29:0], spi_phy_dq_i}` |
| 9624 | `customasicsoc_sr_in <= {customasicsoc_sr_in[30:0], customasicsoc_dq_i[1]}` |
| 9627, 9630, 9633 | `customasicsoc_sr_in <= {customasicsoc_sr_in[29:0], customasicsoc_dq_i}` |

**5c. Drop the carry bit:**

| Line | Change |
|---|---|
| 5597 | `wire _unused_0; assign {_unused_0, spi_phy_half} = ((spi_phy_div + 1'd1) >>> 1'd1);` |
| 7171 | `wire _unused_1; assign {_unused_1, customasicsoc_half} = ((customasicsoc_div + 1'd1) >>> 1'd1);` |

**5d. Port:** line 10796, picorv32 `.pcpi_rd(1'd0)` → `.pcpi_rd(32'd0)`.

## 6. Case defaults
Both are sequential `case (*_last_sink_width)` blocks:

| Line | Added |
|---|---|
| 9482 | `default: spi_phy_sr_in <= 32'b0;` |
| 9622 | `default: customasicsoc_sr_in <= 32'd0;` |

## 7. Memories
All `initial` / `$readmemh` blocks are removed.

**`rom`** (lines 10585–10596, 1024×32): becomes an instance of `rom.v`:
```verilog
wire [31:0] rom_q;
rom rom_inst (.clk(sys_clk), .addr(customasicsoc_customasicsoc_adr[9:0]), .o(rom_q));
assign customasicsoc_customasicsoc_dat_r = rom_q[31:0];
```
(The first pass used an `SPRAM_1024x36` here; it's left commented out in `core.v`.)

**`sram`** (lines 10600–10615) and **`main_ram`** (lines 10619–10634), 8192×32 each: become 4 × `SPRAM_8192x8`, one per byte lane:
```verilog
wire [31:0] main_ram_dat_r_wire;
genvar main_ram_li;
generate
  for (main_ram_li = 0; main_ram_li < 4; main_ram_li = main_ram_li + 1) begin : main_ram_byte_lane
    SPRAM_8192x8 main_ram_spram (
      .A   (customasicsoc_customasicsoc_sram1_adr[12:0]),
      .CE  (~sys_clk),
      .WEB (~customasicsoc_customasicsoc_sram1_we[main_ram_li]),
      .OEB (1'b0),
      .CSB (1'b0),
      .I   (customasicsoc_customasicsoc_sram1_dat_w[8*main_ram_li +: 8]),
      .O   (main_ram_dat_r_wire[8*main_ram_li +: 8]));
  end
endgenerate
assign customasicsoc_customasicsoc_sram1_dat_r = main_ram_dat_r_wire;
```
- `sram` is identical, with `sram0_*` signals and the names `sram_li`, `sram_byte_lane`, `sram_spram` and `sram_dat_r_wire`.
- `CE` was `sys_clk` in the first pass. It's inverted to fix the WEB hold violations in Xcelium.

**`mem`** (lines 10638–10648, 42×8 identifier): the `$readmemh("top_mem.init")` becomes:
```verilog
always @(*) begin
  mem[0] = 8'h50; mem[1] = 8'h49; mem[2] = 8'h43; mem[3] = 8'h4F; mem[4] = 8'h52; mem[5] = 8'h56; mem[6] = 8'h33; mem[7] = 8'h32; // PICORV32
  mem[8] = 8'h20; mem[9] = 8'h42; mem[10]= 8'h41; mem[11]= 8'h53; mem[12]= 8'h45; mem[13]= 8'h44; mem[14]= 8'h20;                 // BASED
  mem[15]= 8'h4D; mem[16]= 8'h43; mem[17]= 8'h55; mem[18]= 8'h20; mem[19]= 8'h49; mem[20]= 8'h49; mem[21]= 8'h54;                 // MCU IIT
  mem[22]= 8'h47; mem[23]= 8'h4E; mem[24]= 8'h20; mem[25]= 8'h50; mem[26]= 8'h52; mem[27]= 8'h4F; mem[28]= 8'h4A;                 // GN PROJ
  mem[29]= 8'h45; mem[30]= 8'h43; mem[31]= 8'h54; mem[32]= 8'h20; mem[33]= 8'h4D; mem[34]= 8'h41; mem[35]= 8'h44;                 // ECT MAD
  mem[36]= 8'h48; mem[37]= 8'h41; mem[38]= 8'h56; mem[39]= 8'h41; mem[40]= 8'h00; mem[41]= 8'h00;                                 // HAVA + pads
end
```
The read logic (`mem_adr0`, `csr_bankarray_dat_r`) is unchanged.

## 8. New files

**`rom.v`**
- Module `rom(clk, addr[9:0], o[31:0])`: an `always @(*)` case table into `data_out`, registered to `o` on `posedge clk`.
- One entry per bootloader word (980 words); `default: 32'h00000013` (NOP).
- Generated by `scripts/bin2rom_v.py firmware/bootloader.bin rtl/rom.v`.

**`top.v`**
- `module top` with one `inout` per pin.
- One `POK_18_18` drives `VDD_OK`/`VDDO_OK` on every pad.
- One `PIOH_18_18` per pin, then `core u_core`.

| Pins | EN | IE | I | CIN | SCHM_ON | PULL_ON, PULL_UP_DOWN |
|---|---|---|---|---|---|---|
| clk, rst | 0 | 1 | 0 | → core | 1 | 0, 0 |
| prog_mode, serial_rx, uart1_rx, spi_miso, spiflash_miso | 0 | 1 | 0 | → core | 0 | 0, 0 |
| serial_tx, uart1_tx, spi_clk/cs_n/mosi, spiflash_clk/cs_n/mosi, pwm_ch_a–d | 1 | 0 | core out | – | 0 | 0, 0 |
| portb/c/d[7:0] | `_oe` | `~_oe` | `_o` | → `_i` | 0 | 0, 0 |
| i2c_scl, i2c_sda | `_oe` | `~_oe` | `_o` | → `_i` | 0 | 1, 1 |

Every pad: `DRIVE_STR=1`, `TTL_ON=0`.
