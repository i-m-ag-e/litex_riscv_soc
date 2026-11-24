# =============================================================================
# testbenches/Makefile — Verilator SoC Testbench Build System
# =============================================================================
# Usage:
#   make                       Build + run tb_gpio_blinky (default)
#   make tb_gpio_blinky        Build + run the GPIO blinky warm-boot test
#   make compile_gpio_blinky   Compile only (don't run)
#   make clean                 Remove all build artifacts
#
# Paths are relative to the testbenches/ directory.
# The .init files are read from the project root (one level up) via symlinks
# or Verilator's working directory — see note below.
# =============================================================================

# ── Paths ────────────────────────────────────────────────────────────────────
PROJ_ROOT   := ..
RTL         := $(PROJ_ROOT)/rtl/sim/asic_top_fixed.v
PICORV32    := $(PROJ_ROOT)/rtl/picorv32.v
TIMER1      := $(PROJ_ROOT)/rtl/timer1.v

# ── Verilator common flags ───────────────────────────────────────────────────
# -Wno-* flags suppress known-harmless lint warnings from the generated netlist
# and the PicoRV32 core (width expansion, implicit nets, CASEINCOMPLETE, etc.)
#
# NOTE: Verilator is NOT a true 4-state simulator. We drop --binary and use
# --main --exe --build (same runnable result), plus --x-initial/--x-assign
# unique.  Combined with the runtime $(RUNARGS) below (+verilator+rand+reset+2),
# uninitialized/X state is filled with random values instead of 0 — exposing any
# reliance-on-zero-init (the same class of bug a 4-state sim like Xcelium finds).
VFLAGS := --timing --main --exe --build \
           --x-initial unique --x-assign unique \
           -Wno-IMPLICIT \
           -Wno-CASEINCOMPLETE \
           -Wno-WIDTHEXPAND \
           -Wno-COMBDLY \
           -Wno-PINMISSING \
           -Wno-WIDTHTRUNC \
           -Wno-UNDRIVEN \
           -Wno-UNUSEDPARAM \
           -Wno-UNUSEDSIGNAL \
           -Wno-LATCH \
           -Wno-MULTIDRIVEN \
           -Wno-SELRANGE \
           -Wno-TIMESCALEMOD

# Runtime args for every sim: randomize uninitialized/X state (mode 2), with a
# fixed seed for reproducibility. Set RANDSEED=<n> to vary. Use rand+reset+0 to
# fall back to the old zero-fill behavior.
RANDSEED ?= 1
RUNARGS  := +verilator+rand+reset+2 +verilator+seed+$(RANDSEED)

# ── Default target ────────────────────────────────────────────────────────────
.PHONY: all
all: tb_gpio_blinky tb_uart_upload

# ── GPIO Blinky — Warm Boot + Firmware Execution Test ────────────────────────
# The .init files live in $(PROJ_ROOT)/ — Verilator runs from this dir, so we
# use +incdir and --Mdir to keep build artifacts isolated, and copy/symlink
# the .init files into the Mdir before running.
#
# Simpler approach: run the binary from the project root where .init files are.
.PHONY: tb_gpio_blinky
tb_gpio_blinky: compile_gpio_blinky
	@echo ""
	@echo "=== Generating warm-boot main_ram init (firmware + magic) ==="
	python3 gen_init.py --mode warmboot
	@echo "=== Running tb_gpio_blinky ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_gpio_blinky/sim_gpio_blinky $(RUNARGS) 2>&1 | \
		grep -E "BLINKY|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_gpio_blinky
compile_gpio_blinky:
	@echo "=== Compiling tb_gpio_blinky with Verilator ==="
	verilator $(VFLAGS) \
	  --top-module tb_gpio_blinky \
	  -Mdir obj_gpio_blinky \
	  -o sim_gpio_blinky \
	  tb_gpio_blinky.v \
	  $(RTL) \
	  $(TIMER1) \
	  $(PICORV32)

# ── UART Upload — State C Firmware Upload Test (MODE_RAM) ─────────────────────
.PHONY: tb_uart_upload
tb_uart_upload: compile_uart_upload
	@echo ""
	@echo "=== Generating upload init (zeroed main_ram + separate payload) ==="
	python3 gen_init.py --mode upload
	@echo "=== Running tb_uart_upload ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_uart_upload/sim_uart_upload $(RUNARGS) 2>&1 | \
		grep -E "UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_uart_upload
compile_uart_upload:
	@echo "=== Compiling tb_uart_upload with Verilator ==="
	verilator $(VFLAGS) \
	  --top-module tb_uart_upload \
	  -Mdir obj_uart_upload \
	  -o sim_uart_upload \
	  tb_uart_upload.v \
	  $(RTL) \
	  $(TIMER1) \
	  $(PICORV32)


# ── Peripheral bring-up tests (UART-UPLOAD firmware, watch portb_o) ────────────
# Each target: build its headless firmware, regenerate top_main_ram.init (all
# zeros) + a self-describing <test>_payload.init, then run. The firmware is
# UPLOADED over UART0 (bootloader State C) — main_ram is NOT pre-loaded, because
# the SPRAM main_ram macro cannot be $readmemh-initialized on the ASIC netlist.
# PASS = portb_o reaches 0xAA; FAIL = 0xF0 or watchdog TIMEOUT. The upload takes
# ~105 ms sim time before the firmware runs. Firmware build needs the RISC-V
# toolchain (same as the other firmware).
.PHONY: bringup
bringup: tb_spi_loopback tb_uart1_loopback tb_interrupt tb_pwm tb_i2c

# --- SPI master loopback (spi_mosi -> spi_miso tied in the tb) ---
.PHONY: tb_spi_loopback
tb_spi_loopback: compile_spi_loopback
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_spi_sim tests/test_spi_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_spi_sim.bin --payload spi_payload.init
	@echo "=== Running tb_spi_loopback ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_spi_loopback/sim_spi_loopback $(RUNARGS) 2>&1 | \
		grep -E "SPI|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_spi_loopback
compile_spi_loopback:
	@echo "=== Compiling tb_spi_loopback with Verilator ==="
	verilator $(VFLAGS) --top-module tb_spi_loopback \
	  -Mdir obj_spi_loopback -o sim_spi_loopback \
	  tb_spi_loopback.v $(RTL) $(TIMER1) $(PICORV32)

# --- UART1 loopback (uart1_tx -> uart1_rx tied in the tb) ---
.PHONY: tb_uart1_loopback
tb_uart1_loopback: compile_uart1_loopback
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_uart1_sim tests/test_uart1_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_uart1_sim.bin --payload uart1_payload.init
	@echo "=== Running tb_uart1_loopback ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_uart1_loopback/sim_uart1_loopback $(RUNARGS) 2>&1 | \
		grep -E "UART1|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_uart1_loopback
compile_uart1_loopback:
	@echo "=== Compiling tb_uart1_loopback with Verilator ==="
	verilator $(VFLAGS) --top-module tb_uart1_loopback \
	  -Mdir obj_uart1_loopback -o sim_uart1_loopback \
	  tb_uart1_loopback.v $(RTL) $(TIMER1) $(PICORV32)

# --- Interrupt path (timer0 self-generated IRQ, no tb tie) ---
.PHONY: tb_interrupt
tb_interrupt: compile_interrupt
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_irq_sim tests/test_irq_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_irq_sim.bin --payload irq_payload.init
	@echo "=== Running tb_interrupt ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_interrupt/sim_interrupt $(RUNARGS) 2>&1 | \
		grep -E "IRQ|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_interrupt
compile_interrupt:
	@echo "=== Compiling tb_interrupt with Verilator ==="
	verilator $(VFLAGS) --top-module tb_interrupt \
	  -Mdir obj_interrupt -o sim_interrupt \
	  tb_interrupt.v $(RTL) $(TIMER1) $(PICORV32)

# --- PWM (timer1) — tb measures the pwm_ch_* pin duty cycles directly ---
.PHONY: tb_pwm
tb_pwm: compile_pwm
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_pwm_sim tests/test_pwm_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_pwm_sim.bin --payload pwm_payload.init
	@echo "=== Running tb_pwm ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_pwm/sim_pwm $(RUNARGS) 2>&1 | \
		grep -E "PWM|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_pwm
compile_pwm:
	@echo "=== Compiling tb_pwm with Verilator ==="
	verilator $(VFLAGS) --top-module tb_pwm \
	  -Mdir obj_pwm -o sim_pwm \
	  tb_pwm.v $(RTL) $(TIMER1) $(PICORV32)

# --- I2C master — tb instantiates an I2C slave model that ACKs/echoes ---
.PHONY: tb_i2c
tb_i2c: compile_i2c
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_i2c_sim tests/test_i2c_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_i2c_sim.bin --payload i2c_payload.init
	@echo "=== Running tb_i2c ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_i2c/sim_i2c $(RUNARGS) 2>&1 | \
		grep -E "I2C|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_i2c
compile_i2c:
	@echo "=== Compiling tb_i2c with Verilator ==="
	verilator $(VFLAGS) --top-module tb_i2c \
	  -Mdir obj_i2c -o sim_i2c \
	  tb_i2c.v $(RTL) $(TIMER1) $(PICORV32)

# --- I2C master — Option A: vendored Forencich i2c_slave.v (MIT) as the slave ---
# Same firmware (test_i2c_sim) as tb_i2c; only the slave model differs.
.PHONY: tb_i2c_fslave
tb_i2c_fslave: compile_i2c_fslave
	@echo ""
	$(MAKE) -C $(PROJ_ROOT)/firmware APP=tests/test_i2c_sim tests/test_i2c_sim.bin
	python3 gen_init.py --mode upload --fw firmware/tests/test_i2c_sim.bin --payload i2c_payload.init
	@echo "=== Running tb_i2c_fslave ==="
	cd $(PROJ_ROOT) && ./testbenches/obj_i2c_fslave/sim_i2c_fslave $(RUNARGS) 2>&1 | \
		grep -E "I2C|UPLOAD|BOOT|PASS|FAIL|TIMEOUT|finish"

.PHONY: compile_i2c_fslave
compile_i2c_fslave:
	@echo "=== Compiling tb_i2c_fslave with Verilator ==="
	verilator $(VFLAGS) --top-module tb_i2c_fslave \
	  -Mdir obj_i2c_fslave -o sim_i2c_fslave \
	  tb_i2c_fslave.v i2c_slave.v $(RTL) $(TIMER1) $(PICORV32)

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf obj_gpio_blinky obj_uart_upload \
	       obj_spi_loopback obj_uart1_loopback obj_interrupt \
	       obj_pwm obj_i2c obj_i2c_fslave
