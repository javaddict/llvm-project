# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld -T %S/haydn.ld %t.o -o %t
# RUN: llvm-readobj -h %t | FileCheck %s
# RUN: llvm-readobj -S %t | FileCheck %s --check-prefix=SECTIONS

# Test that linking with linker script works for Haydn target
# Verifies:
# 1. ELF header is valid
# 2. Sections (.text, .data, .bss) are placed correctly

# CHECK: Format: elf32-unknown
# CHECK: Machine: 0x103

# SECTIONS: Name: .text
# SECTIONS-NEXT: Type: SHT_PROGBITS
# SECTIONS-NEXT: Flags [
# SECTIONS-NEXT: SHF_ALLOC
# SECTIONS-NEXT: SHF_EXECINSTR
# SECTIONS-NEXT: ]
# SECTIONS: Name: .data
# SECTIONS-NEXT: Type: SHT_PROGBITS
# SECTIONS-NEXT: Flags [
# SECTIONS-NEXT: SHF_ALLOC
# SECTIONS-NEXT: SHF_WRITE
# SECTIONS-NEXT: ]
# SECTIONS: Name: .bss
# SECTIONS-NEXT: Type: SHT_NOBITS
# SECTIONS-NEXT: Flags [
# SECTIONS-NEXT: SHF_ALLOC
# SECTIONS-NEXT: SHF_WRITE
# SECTIONS-NEXT: ]

    .globl _start
    .type _start, @function
_start:
    addi32 r0, r0, 0
    addi32 r1, r0, 0
    addi32 r0, r0, 42

    .size _start, .-_start

# Test data sections
    .globl test_data
    .data
    .type test_data, @object
    .size test_data, 4
    .p2align 2
test_data:
    .byte 0x78
    .byte 0x56
    .byte 0x34
    .byte 0x12

# Test BSS (zero-initialized data)
    .globl test_bss
    .comm test_bss, 4, 4
