/* HAYDN-LD-IDENT: 2026-08-14 Format E 12-byte parcel */
/* BundleSim static ILP32 executable layout for Haydn Format E (96-bit).
 *
 * Instruction geometry (must stay consistent with BundleSim config.h):
 *   - Arch PC / instruction align: 16-bit (2 bytes)
 *   - Product record size:         12 bytes (Format E only)
 *   - Code packing preference:     4-byte (power-of-two; not 16)
 *   - ABI data / stack align:      16 bytes (unrelated to encoding)
 *
 * .text size is padded to a whole number of 12-byte Format E parcels so the
 * loader/sim can cover executable bytes with fixed product records.
 *
 * Do not use ALIGN(12): LLD requires power-of-two alignment. Pad size with
 * arithmetic relative to __text_start instead (VMA % 12 need not be 0).
 * Do not reintroduce Bundle128 ALIGN(16) on executable code.
 */
OUTPUT_ARCH(haydn)
ENTRY(_start)

/* Override these policy defaults with --defsym when a machine profile differs. */
PROVIDE(BUNDLESIM_TEXT_BASE = 0x00010000);
PROVIDE(BUNDLESIM_PAGE_SIZE = 0x00001000);
PROVIDE(BUNDLESIM_STACK_TOP = 0x80000000);
PROVIDE(BUNDLESIM_STACK_SIZE = 0x00100000);
PROVIDE(BUNDLESIM_STACK_GUARD_SIZE = 0x00001000);

/* Format E / arch geometry (override only with a matching BundleSim product). */
PROVIDE(BUNDLESIM_INSTR_ALIGN = 2);     /* 16-bit architectural PC align */
PROVIDE(BUNDLESIM_PACK_ALIGN = 4);      /* preferred code packing (power of two) */
PROVIDE(BUNDLESIM_RECORD_BYTES = 12);   /* Format E parcel size (not a section ALIGN) */
PROVIDE(BUNDLESIM_ABI_ALIGN = 16);      /* stack / data ABI, not encoding */

PHDRS
{
    text PT_LOAD FLAGS(5);       /* read, execute */
    rodata PT_LOAD FLAGS(4);     /* read */
    data PT_LOAD FLAGS(6);       /* read, write */
    stack PT_GNU_STACK FLAGS(6); /* read, write, non-executable */
}

SECTIONS
{
    . = BUNDLESIM_TEXT_BASE;
    . = ALIGN(BUNDLESIM_PACK_ALIGN);
    __text_start = .;
    .text : ALIGN(BUNDLESIM_PACK_ALIGN)
    {
        KEEP(*(.text.startup .text.startup.*))
        *(.text .text.*)
        /*
         * Pad executable *size* to whole Format E parcels.
         * ALIGN(BUNDLESIM_RECORD_BYTES) is illegal (12 is not a power of two)
         * and would also require VMA % 12 == 0 for size % 12 == 0.
         */
        . = . + ((BUNDLESIM_RECORD_BYTES -
                  ((. - __text_start) % BUNDLESIM_RECORD_BYTES))
                 % BUNDLESIM_RECORD_BYTES);
    } :text
    __text_end = .;

    . = ALIGN(BUNDLESIM_PAGE_SIZE);
    __rodata_start = .;
    .rodata : ALIGN(BUNDLESIM_ABI_ALIGN)
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        /* Pad to 8-byte boundary so word-granularity strlen/memchr reads
         * do not overrun the mapped segment when the last object is
         * unaligned (CB-141). */
        . = ALIGN(8);
    } :rodata

    .preinit_array : ALIGN(4)
    {
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.preinit_array.*)))
        __preinit_array_end = .;
    } :rodata

    .init_array : ALIGN(4)
    {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :rodata

    .fini_array : ALIGN(4)
    {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    } :rodata

    .eh_frame_hdr : ALIGN(4) { *(.eh_frame_hdr) } :rodata
    .eh_frame : ALIGN(4) { KEEP(*(.eh_frame)) } :rodata
    __rodata_end = .;

    . = ALIGN(BUNDLESIM_PAGE_SIZE);
    __data_start = .;
    .data : ALIGN(BUNDLESIM_ABI_ALIGN)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.* .got.plt)
    } :data

    .tdata : ALIGN(BUNDLESIM_ABI_ALIGN) { *(.tdata .tdata.*) } :data
    .tbss (NOLOAD) : ALIGN(BUNDLESIM_ABI_ALIGN) { *(.tbss .tbss.*) } :data
    __data_end = .;

    .bss (NOLOAD) : ALIGN(BUNDLESIM_ABI_ALIGN)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(BUNDLESIM_ABI_ALIGN);
        __bss_end = .;
    } :data

    __image_end = .;
    __heap_start = ALIGN(__bss_end, BUNDLESIM_PAGE_SIZE);

    __stack_top = BUNDLESIM_STACK_TOP;
    __stack_bottom = __stack_top - BUNDLESIM_STACK_SIZE;
    __stack_guard_bottom = __stack_bottom - BUNDLESIM_STACK_GUARD_SIZE;
    __heap_end = __stack_guard_bottom;

    /* llvm-libc freelist malloc (baremetal): region [_end, __llvm_libc_heap_limit).
     * Alias image/heap ends so plat does not need a second heap policy. */
    PROVIDE(_end = __heap_start);
    PROVIDE(__llvm_libc_heap_limit = __heap_end);

    .note.GNU-stack (INFO) :
    {
        KEEP(*(.note.GNU-stack))
    } :stack

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
    }
}

/* Geometry gates — keep in lockstep with bundlesim/include/bundlesim/config.h */
ASSERT((BUNDLESIM_INSTR_ALIGN & (BUNDLESIM_INSTR_ALIGN - 1)) == 0,
       "BundleSim instruction align must be a power of two")
ASSERT((BUNDLESIM_PACK_ALIGN & (BUNDLESIM_PACK_ALIGN - 1)) == 0,
       "BundleSim code packing align must be a power of two")
ASSERT((BUNDLESIM_PACK_ALIGN % BUNDLESIM_INSTR_ALIGN) == 0,
       "BundleSim packing align must be a multiple of instruction align")
ASSERT((BUNDLESIM_RECORD_BYTES % BUNDLESIM_INSTR_ALIGN) == 0,
       "BundleSim product record size must be a multiple of instruction align")
ASSERT((BUNDLESIM_TEXT_BASE & (BUNDLESIM_INSTR_ALIGN - 1)) == 0,
       "BundleSim text base must be 16-bit aligned")
ASSERT((__text_start & (BUNDLESIM_INSTR_ALIGN - 1)) == 0,
       "BundleSim executable section start must be 16-bit aligned")
ASSERT(((__text_end - __text_start) % BUNDLESIM_RECORD_BYTES) == 0,
       "BundleSim text size must be a multiple of Format E record size (12)")
ASSERT((BUNDLESIM_PAGE_SIZE & (BUNDLESIM_PAGE_SIZE - 1)) == 0,
       "BundleSim page size must be a power of two")
ASSERT((__stack_top & (BUNDLESIM_ABI_ALIGN - 1)) == 0,
       "BundleSim stack top must be ABI-aligned (16)")
ASSERT((__stack_bottom & (BUNDLESIM_ABI_ALIGN - 1)) == 0,
       "BundleSim stack bottom must be ABI-aligned (16)")
ASSERT(BUNDLESIM_STACK_SIZE >= BUNDLESIM_PAGE_SIZE,
       "BundleSim stack must contain at least one page")
ASSERT(BUNDLESIM_STACK_GUARD_SIZE >= BUNDLESIM_PAGE_SIZE,
       "BundleSim stack guard must contain at least one page")
ASSERT(BUNDLESIM_STACK_TOP >=
       BUNDLESIM_STACK_SIZE + BUNDLESIM_STACK_GUARD_SIZE,
       "BundleSim stack and guard underflow the guest address space")
ASSERT((BUNDLESIM_STACK_SIZE & (BUNDLESIM_PAGE_SIZE - 1)) == 0,
       "BundleSim stack size must be page aligned")
ASSERT((BUNDLESIM_STACK_GUARD_SIZE & (BUNDLESIM_PAGE_SIZE - 1)) == 0,
       "BundleSim stack guard size must be page aligned")
ASSERT(__heap_start <= __heap_end,
       "BundleSim image, heap, guard, and stack overlap")
ASSERT(__image_end <= 0xffffffff,
       "BundleSim image is outside the 32-bit guest address space")
ASSERT(__stack_top <= 0xffffffff,
       "BundleSim stack is outside the 32-bit guest address space")
ASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0,
       "BundleSim does not support ELF TLS sections")
