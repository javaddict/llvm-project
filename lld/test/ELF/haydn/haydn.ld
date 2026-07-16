/* haydn.ld - Linker script for Haydn baremetal programs */
/* Memory: 1MB RAM at 0x10000, stack at top */

OUTPUT_FORMAT("elf32-haydn")
OUTPUT_ARCH(haydn)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000, LENGTH = 1M
}

SECTIONS
{
    .text : {
        *(.crt0)
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > RAM

    .data : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : {
        __stack_bottom = .;
        . = . + 0x10000;
        __stack_top = .;
    } > RAM

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.comment)
        *(.eh_frame*)
    }
}
