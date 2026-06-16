.global _start
.section .text.start

_start:
    /* Initialize the Stack Pointer to the top of SRAM */
    li sp, 0x01002000
    
    /* Jump to the C main function */
    call main
    
halt:
    j halt
