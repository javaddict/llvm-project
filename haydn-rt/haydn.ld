//===-- haydn.ld - Haydn baremetal linker script ----------------*- ld -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Linker script for Haydn baremetal programs.
// This defines the memory layout and section placement.
//
// Memory configuration:
//   - RAM starts at 0x10000 (64KB aligned, configurable)
//   - No ROM/flash (all code in RAM for simulation/QEMU)
//   - Stack grows downward from end of RAM
//   - 8-byte stack alignment per LLVM hard constraint
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Memory Configuration
//===----------------------------------------------------------------------===//

MEMORY
{
    /* RAM: Read-write memory for code, data, and stack
     * Origin: 0x10000 (64KB aligned, avoids special addresses)
     * Length: 1MB (adjust for actual hardware) */
    RAM (rwx) : ORIGIN = 0x10000, LENGTH = 1M
}

//===----------------------------------------------------------------------===//
// Section Definitions
//===----------------------------------------------------------------------===//

SECTIONS
{
    /* .text: Code section
     * Contains executable instructions */
    .text : {
        /* Start of code */
        *(.crt0)
        *(.text)

        /* Read-only data inline with code */
        *(.rodata)
        *(.rodata*)

        /* Keep these sections for C++ support (if needed) */
        KEEP(*(.init))
        KEEP(*(.fini))

        /* EH frame for exception handling (optional) */
        *(.eh_frame*)
    } > RAM

    /* Enforce 8-byte alignment between sections (HC#4: 8-byte stack alignment) */
    . = ALIGN(8);

    /* .data: Initialized data section
     * Contains global/static variables with initial values
     * VMA (virtual address) in RAM, LMA (load address) same as VMA */
    .data : {
        __data_start = .;
        *(.data)
        *(.data*)

        /* C++ support */
        *(.data.rel.ro)
        *(.data.rel.ro.local*)

        __data_end = .;
    } > RAM

    /* Enforce 8-byte alignment between sections (HC#4: 8-byte stack alignment) */
    . = ALIGN(8);

    /* .bss: Zero-initialized data section
     * Contains uninitialized global/static variables
     * NOBITS section - doesn't take space in the binary */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss*)
        *(COMMON)  /* Uninitialized global variables */
        __bss_end = .;
    } > RAM

    /* Enforce 8-byte alignment between sections (HC#4: 8-byte stack alignment) */
    . = ALIGN(8);

    /* Stack: Reserved space for stack
     * Stack grows downward from __stack_top */
    .stack (NOLOAD) : {
        __stack_bottom = .;
        . = . + 0x10000;  /* 64KB stack space (configurable) */
        __stack_top = .;
    } > RAM

    /* Discard unwind sections if not needed */
    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.interp)
        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
    }
}

//===----------------------------------------------------------------------===//
// Exported Symbols
//===----------------------------------------------------------------------===//

/*
 * Global symbols available to C code:
 *
 *   __bss_start  - Start of BSS section (used by crt0 to clear memory)
 *   __bss_end    - End of BSS section
 *   __stack_top  - Top of stack (initial value for SP)
 *   __stack_bottom - Bottom of stack (for stack overflow detection)
 */
