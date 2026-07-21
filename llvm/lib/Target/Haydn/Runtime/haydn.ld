/* Haydn baremetal linker script */
/* Default memory layout: 64KB RAM starting at 0x00000000 */

OUTPUT_FORMAT("elf32-haydn", "elf32-haydn", "elf32-haydn")
OUTPUT_ARCH(haydn)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
    /* Startup code first */
    .text.startup : {
        *(.text.startup)
        *(.text.startup.*)
    } > RAM

    /* Main code */
    .text : {
        *(.text .text.*)
    } > RAM

    /* Read-only data */
    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM

    /* Initialized data */
    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
        PROVIDE(__global_pointer$ = . + 0x800);
    } > RAM

    /* BSS */
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* Stack grows downward from top of RAM */
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}
