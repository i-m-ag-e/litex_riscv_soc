# PicoRV32 SoC (LiteX → SCL180 ASIC)

PicoRV32 SoC with UART ×2, SPI, SPI flash, I2C, GPIO (portb/c/d), timer0, timer1 (PWM) and a watchdog.
It was built with LiteX, prototyped on a Basys3, then hand-cleaned for the SCL180 ASIC flow.

## The SoC is frozen

`rtl/core.v` is hand-edited LiteX output and is the source of truth.

- **Don't regenerate the RTL with `asic_soc.py`.** It would undo the hand edits, and a different LiteX version can shift the CSR map.
- **Firmware builds against the committed `build/asic_soc/`.** This folder holds the CSR map (`csr.h`, `csr.csv`) and memory map (`regions.ld`) that match `core.v`. Don't overwrite it.
- **If the hardware must change,** edit `rtl/core.v` directly, then update `docs/asic_rtl_fixups_detailed.md`.

## Layout

```
rtl/
  top.v               chip top: SCL180 pads + core
  core.v              SoC (cleaned LiteX output)
  rom.v               bootloader ROM (case-statement table)
  picorv32.v          CPU (upstream, unmodified)
  timer1.v            PWM timer
  sim/asic_top_fixed.v  older core with behavioural memories, for Verilator
  reference/asic_top.v  raw LiteX output, the baseline for docs/
build/asic_soc/       generated CSR map + C headers (frozen)
firmware/             bootloader, apps, tests/, uploader.py; shrike_slave/ = RP2040 test partner
testbenches/          Verilator testbenches
fpga/                 Basys3 wrapper + constraints (wraps rtl/sim/asic_top_fixed.v)
scripts/bin2rom_v.py  bootloader.bin → rtl/rom.v
docs/                 how the RTL was cleaned + exact change list + lint/sim commands
asic_soc.py           LiteX generator (reference only, see above)
basys3_soc.py         original Basys3 FPGA SoC
timer1_core.py, watchdog.py   LiteX wrappers for timer1 / watchdog
top_*.init            ROM/SRAM/identifier init files for the behavioural sim
```

## Firmware

Needs only `riscv64-unknown-elf-gcc` (no LiteX install).

```bash
cd firmware
make                          # bootloader.bin + app_main.bin
make APP=tests/test_gpio      # any .c as the app
```

If you change the bootloader, regenerate the ROM:

```bash
python3 scripts/bin2rom_v.py firmware/bootloader.bin rtl/rom.v
```

## Simulation

- **Verilator** (no PDK needed; uses `rtl/sim/asic_top_fixed.v`):
  ```bash
  make -C testbenches all bringup
  ```
  A test passes when `portb_o == 0xAA`.
- **Xcelium + PDK** (final RTL with SPRAM macros): see `docs/synthesizable_verilog_from_litex.md`.

## Docs

- `docs/rtl_cleanup_steps.md`: steps to go from LiteX output to the ASIC RTL.
- `docs/asic_rtl_fixups_detailed.md`: exact changes, with line numbers.
- `docs/synthesizable_verilog_from_litex.md`: coding rules + lint/sim commands (CCL).
