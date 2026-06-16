INCLUDE ../build/digilent_basys3/software/include/generated/regions.ld

ENTRY(_start)

SECTIONS {
    .text : {
        . = ALIGN(4);      /* Ensure 4-byte alignment */
        KEEP(*(.text.start))
        *(.text*)
        *(.rodata*)
    } > main_ram

    .data : {
        . = ALIGN(4);      /* Ensure 4-byte alignment */
        *(.data*)
        *(.sdata*)
    } > main_ram

    .bss : {
        . = ALIGN(4);      /* Align the start pointer */
        _sbss = .;         
        
        *(.bss*)
        *(.sbss*)
        
        . = ALIGN(4);      /* Align the end pointer */
        _ebss = .;         
    } > main_ram
}
