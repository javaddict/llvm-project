/* haydn.ld - Linker script for Haydn baremetal programs */
/* Memory: 1MB RAM at 0x10000, stack at top */
/*
 * Product encoding: Format E sole active family.
 * HAYDN_PRODUCT_PARCEL_BYTES mirrors the production EncodedBytes from the
 * object-encoding registry (currently 12). Text size must be a multiple of
 * that parcel size so images pack with BundleSim ASSERT % record-size.
 *
 * Arch PC / instruction align remains 2; ABI data/stack align remains 16.
 * Product parcel size is not a power of two, so LLD ALIGN(N) cannot pad it
 * (ALIGN requires power-of-two). Use modular location-counter pad instead.
 */

OUTPUT_FORMAT("elf32-haydn")
OUTPUT_ARCH(haydn)
ENTRY(_start)

PROVIDE(HAYDN_INSTR_ALIGN = 2);
PROVIDE(HAYDN_PRODUCT_PARCEL_BYTES = 12);
PROVIDE(HAYDN_ABI_ALIGN = 16);

MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000, LENGTH = 1M
}

SECTIONS
{
    . = ORIGIN(RAM);
    __text_start = .;
    .text : {
        *(.crt0)
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        /* Pad executable size to whole product parcels (non-power-of-two). */
        . = . + (HAYDN_PRODUCT_PARCEL_BYTES -
                 ((. - __text_start) % HAYDN_PRODUCT_PARCEL_BYTES)) %
                HAYDN_PRODUCT_PARCEL_BYTES;
    } > RAM
    __text_end = .;

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
        . = ALIGN(HAYDN_ABI_ALIGN);
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

ASSERT((__text_start & (HAYDN_INSTR_ALIGN - 1)) == 0,
       "Haydn executable section start must be instruction-aligned")
ASSERT(((__text_end - __text_start) % HAYDN_PRODUCT_PARCEL_BYTES) == 0,
       "Haydn text size must be a multiple of product parcel EncodedBytes")
ASSERT((__stack_top & (HAYDN_ABI_ALIGN - 1)) == 0,
       "Haydn stack top must be ABI-aligned")
