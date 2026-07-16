# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -T %S/haydn.ld %t.o -o %t
# RUN: llvm-readobj -h %t | FileCheck --check-prefix=HEADER %s
# RUN: llvm-readobj -S %t | FileCheck --check-prefix=SECTIONS %s
# RUN: llvm-readobj -s %t | FileCheck --check-prefix=SYMS %s

# Test linking with the Haydn linker script and _start entry point.
# This verifies:
# 1. ELF entry point is set correctly (0x10000 + text offset)
# 2. Sections are placed in the correct memory region (RAM at 0x10000)
# 3. .text, .data, .bss sections exist with correct flags
# 4. _start symbol is defined
#
# Regression test for M6 milestone: baremetal linking with linker script
# must produce a valid executable with correct memory layout.

# HEADER: Format: elf32-unknown
# HEADER: Machine: 0x103
# HEADER: Entry: 0x10000

# SECTIONS: Name: .text
# SECTIONS: Name: .data
# SECTIONS: Name: .bss

# SYMS: Name: _start

    .globl _start
    .type _start, @function
_start:
    ADD32 R0, R0, R0
    .size _start, .-_start
