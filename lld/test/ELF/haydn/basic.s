# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t
# RUN: llvm-readobj -h %t | FileCheck %s

# Test that basic linking works for Haydn target

# CHECK: Format: elf32-unknown
# CHECK: Machine: 0x103

.globl _start
_start:
    ADD32 R0, R1, R2
    MOVE32 R2, R3

    .size _start, .-_start
