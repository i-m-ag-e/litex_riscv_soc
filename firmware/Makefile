BUILD_DIR = ../build/digilent_basys3

# Pull in the LiteX directories and CPU flags
include $(BUILD_DIR)/software/include/generated/variables.mak

CC      = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy

# LiteX header paths
INCLUDES = \
	-I$(BUILDINC_DIRECTORY) \
	-I$(CPU_DIRECTORY) \
	-I$(SOC_DIRECTORY)/software/include

# Common compiler flags
CFLAGS = $(CPUFLAGS) -O2 -g -Wall $(INCLUDES) -ffreestanding -fno-builtin

# Targets
BOOT_TARGET = bootloader
APP_TARGET  = firmware

# Source files (Rename your C files accordingly)
BOOT_OBJS = start.o boot_main.o
APP_OBJS  = app_start.o app_main.o 

# Default target builds both binaries
all: $(BOOT_TARGET).bin $(APP_TARGET).bin

# ─── BOOTLOADER BUILD RULES ───
$(BOOT_TARGET).bin: $(BOOT_TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BOOT_TARGET).elf: $(BOOT_OBJS)
	$(CC) $(CFLAGS) -nostdlib -Wl,--no-relax -T linker_boot.ld -o $@ $(BOOT_OBJS)

# ─── FIRMWARE BUILD RULES ───
$(APP_TARGET).bin: $(APP_TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(APP_TARGET).elf: $(APP_OBJS)
	$(CC) $(CFLAGS) -nostdlib -Wl,--no-relax -T linker_app.ld -o $@ $(APP_OBJS) \
		-lgcc

# ─── COMMON COMPILE RULES ───
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.elf *.bin
