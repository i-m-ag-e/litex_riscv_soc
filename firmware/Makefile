BUILD_DIR = ../build/asic_soc

# LiteX build output: generated/csr.h, soc.h, mem.h, regions.ld
BUILDINC_DIRECTORY := $(abspath $(BUILD_DIR)/software/include)

# CPU flags (copied from LiteX's variables.mak for picorv32)
CPUFLAGS = -mno-save-restore -march=rv32i2p0_m -mabi=ilp32 -D__picorv32__

CC      = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy

# include/ holds the two LiteX headers we use (system.h, hw/common.h),
# copied from LiteX ad23dae51 so no LiteX install is needed
INCLUDES = \
	-I$(BUILDINC_DIRECTORY) \
	-I$(CURDIR)/include \
	-Itests \
	-I$(CURDIR)

# Common flags (shared)
COMMON_FLAGS = $(CPUFLAGS) -g -Wall $(INCLUDES) -ffreestanding -fno-builtin

# Bootloader: -Os (must fit in 4 KB ROM)
# App firmware: -O2 (runs from 32 KB SRAM, no size constraint)
BOOT_CFLAGS = $(COMMON_FLAGS) -Os
APP_CFLAGS  = $(COMMON_FLAGS) -O2

# Targets
BOOT_TARGET = bootloader

# ── Selectable application source ────────────────────────────────────────────
APP     ?= app_main
APP_SRC  = $(APP).c
APP_TARGET = $(APP)

# Source files
BOOT_OBJS = start.o boot_main.o
APP_OBJS  = app_start.o $(APP).o isr.o

# ── Default target: bootloader + selected firmware ────────────────────────────
all: $(BOOT_TARGET).bin $(APP_TARGET).bin

# Convenience shorthand: `make firmware-<name>` e.g. `make firmware-tests/test_blink`
firmware-%:
	$(MAKE) APP=$(patsubst firmware-%,%,$@) $(patsubst firmware-%,%,$@).bin

# ─── BOOTLOADER BUILD RULES ───────────────────────────────────────────────────
$(BOOT_TARGET).bin: $(BOOT_TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@printf "  SIZE  bootloader: %d bytes\n" $$(wc -c < $@)

$(BOOT_TARGET).elf: $(BOOT_OBJS)
	$(CC) $(BOOT_CFLAGS) -nostdlib -Wl,--no-relax -L$(BUILDINC_DIRECTORY) -T linker_boot.ld -o $@ $(BOOT_OBJS)

start.o: start.S
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

boot_main.o: boot_main.c
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

# ─── FIRMWARE BUILD RULES ─────────────────────────────────────────────────────
$(APP_TARGET).bin: $(APP_TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@printf "  SIZE  firmware ($(APP)): %d bytes\n" $$(wc -c < $@)

$(APP_TARGET).elf: $(APP_OBJS)
	$(CC) $(APP_CFLAGS) -nostdlib -Wl,--no-relax -L$(BUILDINC_DIRECTORY) -T linker_app.ld -o $@ $(APP_OBJS) \
		-lgcc

app_start.o: app_start.S
	$(CC) $(APP_CFLAGS) -c $< -o $@

isr.o: isr.c
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(APP).o: $(APP_SRC)
	$(CC) $(APP_CFLAGS) -c $< -o $@

# ── Phony helpers ─────────────────────────────────────────────────────────────
.PHONY: all clean help

clean:
	rm -f *.o *.elf *.bin
	rm -f tests/*.o tests/*.elf tests/*.bin

help:
	@echo ""
	@echo "Usage:"
	@echo "  make                         Build bootloader + app_main firmware (default)"
	@echo "  make APP=<name>              Build bootloader + <name>.c as firmware"
	@echo "  make APP=tests/<name>        Build bootloader + tests/<name>.c as firmware"
	@echo "  make firmware-<name>         Shorthand: build firmware from <name>.c"
	@echo "  make bootloader.bin          Build only the bootloader"
	@echo "  make clean                   Remove all build artefacts"
	@echo ""
	@echo "Examples:"
	@echo "  make APP=firmware_timer1"
	@echo "  make APP=tests/test_blink"
	@echo "  make firmware-tests/test_blink"
	@echo ""
