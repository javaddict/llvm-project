# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x20000
# RUN: llvm-objdump -s -j .rodata %t | FileCheck %s

# PIC/JT label-difference entries are R_HAYDN_32_PCREL (R_PC). lld must write
# S+A-P = LBB - JT, never the absolute LBB (R_HAYDN_32 / R_ABS).
#
# RELOCS: R_HAYDN_32_PCREL .Ltarget
# RELOCS-NOT: R_HAYDN_32{{ }}
#
# target@0x1000c - jt@0x20000 = 0xffff000c.
# CHECK: Contents of section .rodata:
# CHECK: 20000 0c00ffff 0c00ffff

.section .text
.globl _start
_start:
    { xor32 r0, r0, r0 }
.Ltarget:
    { xor32 r0, r0, r0 }
    .size _start, .-_start

.section .rodata
.globl jt
jt:
    .long .Ltarget - jt
    .long .Ltarget - jt
    .size jt, .-jt
