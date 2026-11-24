/* regions.ld comes from the LiteX build dir, found via -L$(BUILDINC_DIRECTORY) in the Makefile */
INCLUDE generated/regions.ld

ENTRY(_start)

SECTIONS {
    .text : {
        /* Force the startup code to be the first bytes in ROM */
        KEEP(*(.text.start))
        *(.text*)
        *(.rodata*)
    } > rom

    .data : {
        _fdata_rom = LOADADDR(.data);
        _fdata = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(16);
        _edata = .;
    } > sram AT > rom

    .bss : {
        _fbss = .;
        *(.bss*)
        *(.sbss*)
        . = ALIGN(4);
        _ebss = .;
    } > sram
    
    /* Stack top fixed at sram + 8 KB (the Basys3 SRAM size) so the bootloader
       builds identical to the ROM in rtl/rom.v. The ASIC has 32 KB of SRAM. */
    PROVIDE(_fstack = ORIGIN(sram) + 0x2000);
}
