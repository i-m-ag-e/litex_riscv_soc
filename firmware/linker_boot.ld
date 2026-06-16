INCLUDE ../build/digilent_basys3/software/include/generated/regions.ld

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
    
    /* Set stack pointer to the top of the bootloader's SRAM region */
    PROVIDE(_fstack = ORIGIN(sram) + LENGTH(sram));
}
