# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t1.o
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %S/Inputs/multiple-obj-2.s -o %t2.o
# RUN: ld.lld %t1.o %t2.o -o %t
# RUN: llvm-readobj -S %t | FileCheck %s

# Test linking multiple object files with cross references

# CHECK: Name: .text
# CHECK: Type: SHT_PROGBITS (0x1)
# CHECK: Flags [ (0x6)
# CHECK-NEXT:     SHF_ALLOC (0x2)
# CHECK-NEXT:     SHF_EXECINSTR (0x4)
# CHECK: Size: 16

.globl _start
_start:
    ADDI32 R1, R0, 42
    MOVE32 R3, R4

# Reference to function2 in second object file
    .long function2

    .size _start, .-_start
