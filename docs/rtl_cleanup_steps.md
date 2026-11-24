# LiteX SoC → ASIC RTL: Cleanup Steps

Start: `build/asic_soc/gateware/asic_top.v` (raw LiteX output, copy kept in `rtl/reference/asic_top.v`).
End: `rtl/top.v` (pad wrapper) + `rtl/core.v` + `rtl/rom.v` + `rtl/picorv32.v` + `rtl/timer1.v`.

Do every bulk edit with a script, not an LLM, since an LLM can silently change logic.

---

## A. Generate the netlist (asic_soc.py)

1. Use `GenericPlatform` with a custom `build()` that only writes the Verilog, since there's no FPGA toolchain to run.
2. In `_io`, split every bidirectional pin into `_i`, `_o` and `_oe` (I2C, portb/c/d). The pads do the tristate, not the core.
3. Use a custom CRG that wires the `clk`/`rst` pins straight to the `sys` domain (no PLL).
4. Build with `regular_comb=False, hierarchical=False` to get one flat module that's easy to edit.
5. Build the bootloader first, since the ROM init is read from `firmware/bootloader.bin`:
   ```bash
   make -C firmware bootloader.bin
   python3 asic_soc.py
   ```
6. Copy `asic_top.v` to `rtl/reference/` before editing, so you always have a clean baseline to diff against.

## B. Clean `asic_top.v` → `core.v`

1. Rename `module top` to `module core` and save it as `core.v`, since `top` is now the pad wrapper.
2. Inside every `always @(*)` block, change `<=` to `=`. Skip `<=` used as a comparison inside `if (...)`. The nonblocking assignments caused glitches on the SRAM write-enable in Xcelium.
3. Delete every declaration initializer, so `reg [n:0] x = val;` becomes `reg [n:0] x;`. Silicon has no power-up values.
4. For every flop (any signal assigned in `always @(posedge sys_clk)`), add `x <= val;` to the main `if (sys_rst) begin` block. Use the initializer you just deleted, or `0` if it had none. The only flop left without a reset is `mem_adr0`, the read address for the identifier string.
   - The ones LiteX had left reset-less are:
     - the UART rx/tx `data/count/phase` (both UARTs);
     - the SPI `sr_in_cnt/sr_out_cnt` (both PHYs);
     - every `csr_bankarray_interfaceN_bank_bus_dat_r`;
     - the `mmap_burst_adr/byte_count` pair;
     - the `*_fifo_pipe_valid_*` signals;
     - all the `multiregimpl*` signals, although these resets have no effect, since the input-sync assignments after the reset block override them;
     - `count <= 20'd1000000`.
5. Some regs are declared but never assigned:
   - `*_bus_err`, `*_source_first/last`, `*_fifo_replace` and `*_adr_burst`;
   - `*_en_int`, `*_no_read`, `interface0_err` and `customasicsoc_mmap_dummy`.

   Put these in the same reset block with their constant value (`mmap_dummy <= 32'd57005`), since nothing else drives them.
   - Also add `customasicsoc_dq_i[0] <= 0` and `spi_phy_dq_i[0] <= 0` for the unused bit.
   - An earlier version used a separate `always @(*)` tie-off block for these regs. Don't use it; the reset block is the final approach.
6. For the UART FIFO memories `storage`, `storage_1`, `storage_2` and `storage_3` (16×10 each):
   - Merge the two `always @(posedge sys_clk)` blocks of each one into a single block.
   - At the top of that block, add a reset that clears the memory and both output regs:
     ```verilog
     if (sys_rst) begin : reset_storage
         integer i;
         storage_dat0 <= 10'd0; storage_dat1 <= 10'd0;
         for (i = 0; i < 16; i = i + 1) storage[i] <= 10'd0;
     end
     ```
7. Add a `default:` to both `case (*_last_sink_width)` blocks so they don't infer a latch:
   - `default: spi_phy_sr_in <= 32'b0;`
   - `default: customasicsoc_sr_in <= 32'd0;`
8. Make implicit widths explicit with `{N'd0, x}` and `x[hi:lo]`, and change `.pcpi_rd(1'd0)` to `.pcpi_rd(32'd0)`. This only silences lint; behaviour is unchanged. The full list is in `asic_rtl_fixups_detailed.md`.
9. Delete every `initial` block and every `$readmemh`, then replace each memory:
   - **`mem`** (the 42×8 identifier string): fill it with constants in an `always @(*)` block, for example `mem[0] = 8'h50; ...`. It's small enough not to need a macro.
   - **`rom`** (1024×32 bootloader): the SCL PDK has no ROM macro, so use a case-statement ROM instead:
     ```verilog
     wire [31:0] rom_q;
     rom rom_inst (.clk(sys_clk), .addr(customasicsoc_customasicsoc_adr[9:0]), .o(rom_q));
     assign customasicsoc_customasicsoc_dat_r = rom_q[31:0];
     ```
     Generate `rom.v` with this command, and re-run it every time the bootloader changes:
     ```bash
     python3 scripts/bin2rom_v.py firmware/bootloader.bin rtl/rom.v
     ```
   - **`sram`** and **`main_ram`** (8192×32 each): replace each one with 4 byte-lane `SPRAM_8192x8` macros:
     ```verilog
     for (li = 0; li < 4; li = li + 1) begin : main_ram_byte_lane
       SPRAM_8192x8 main_ram_spram (
         .A  (customasicsoc_customasicsoc_sram1_adr[12:0]),
         .CE (~sys_clk),                                    // inverted clock
         .WEB(~customasicsoc_customasicsoc_sram1_we[li]),   // active-low
         .OEB(1'b0), .CSB(1'b0),
         .I  (customasicsoc_customasicsoc_sram1_dat_w[8*li +: 8]),
         .O  (main_ram_dat_r_wire[8*li +: 8]));
     end
     ```
     - `sram` is the same block with the `sram0_*` signals.
     - Inverting `CE` fixed the WEB hold violations seen in Xcelium.
10. Sanity check: each of these commands should print `0`:
    ```bash
    grep -c '\binitial\b' rtl/core.v
    grep -c 'readmemh' rtl/core.v
    grep -cE '^\s*reg\b[^;]*=' rtl/core.v
    ```

## C. Pad wrapper `top.v`

1. Create a new `module top` with one `inout` per physical pin.
2. Instantiate `POK_18_18` and wire its `VDD_OK`/`VDDO_OK` to every pad.
3. Instantiate one `PIOH_18_18` per pin:
   - **Input pins** (clk, rst, rx, miso, prog_mode): `EN=0`, `IE=1`, `I=0`, and `CIN` goes to the core input.
   - **Output pins** (tx, spi clk/cs/mosi, pwm): `EN=1`, `IE=0`, and `I` comes from the core output.
   - **Bidirectional pins** (I2C, portb/c/d): `EN=oe`, `IE=~oe`, `I=_o`, and `CIN` goes to `_i`.
4. Set these pad options:
   - `SCHM_ON=1` only on clk and rst, for clean edges.
   - `PULL_ON=1` and `PULL_UP_DOWN=1` only on I2C, since I2C needs pull-ups.
   - `DRIVE_STR=1` and `TTL_ON=0` on every pad.
5. Instantiate `core u_core` and connect all the split ports.

## D. Verify

1. Lint with Xcelium HAL or JasperGold SuperLint. The commands are in `synthesizable_verilog_from_litex.md`.
2. Simulate in Xcelium with the PDK SPRAM models. Don't use `initreg0` or any zero-init option, since a 4-state sim is what exposes missing resets.
3. Without the PDK, run the Verilator testbenches:
   ```bash
   make -C testbenches all bringup
   ```
   - They use `rtl/sim/asic_top_fixed.v`, which has behavioural memories.
   - They run with `--x-initial unique` and `+verilator+rand+reset+2`, so uninitialized state is random rather than zero.
4. A pass means `portb_o == 0xAA` after a UART firmware upload, which proves the uploaded firmware actually ran.

---

Other notes: Krishang's RTL-change pages are [page 1](https://languid-suit-427.notion.site/RTL-Changes-37b28a1af16d807a91dbfd23c08e3101) and [page 2](https://languid-suit-427.notion.site/RTL-Changes-38228a1af16d80c1abf4ef935767bd28).
