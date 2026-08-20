# REQUIRES: haydn-registered-target
# RUN: rm -rf %t && split-file %s %t
#
# 8/16-bit PC-relative data has no Haydn ELF kind. Label-difference
# `.short/.byte S - P` must not silently mint absolute R_HAYDN_16/8
# (that would write S, not S-P). Peer: RISCVELFObjectWriter.cpp:77-83
# IsPCRel only on FK_Data_4. 32-bit PIC/JT stays R_HAYDN_32_PCREL
# (reloc-pic-jt-label-diff.s). Do not invent R_HAYDN_16_PCREL.
#
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %t/short.s \
# RUN:     -o %t/short.o 2>&1 | FileCheck %s --check-prefix=SHORT
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %t/byte.s \
# RUN:     -o %t/byte.o 2>&1 | FileCheck %s --check-prefix=BYTE
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %t/abs.s -o %t/abs.o
# RUN: llvm-readobj -r %t/abs.o | FileCheck %s --check-prefix=ABS

# SHORT: error: 16-bit PC-relative data relocations are not supported on Haydn
# BYTE: error: 8-bit PC-relative data relocations are not supported on Haydn
# ABS: R_HAYDN_16 ext
# ABS-NOT: R_HAYDN_SImm16
# ABS-NOT: R_HAYDN_32_PCREL

#--- short.s
    .section .text
    .globl _start
_start:
    { xor32 r0, r0, r0 }
.Ltarget:
    { xor32 r0, r0, r0 }

    .section .rodata
    .globl jt
jt:
    .short .Ltarget - jt

#--- byte.s
    .section .text
    .globl _start
_start:
    { xor32 r0, r0, r0 }
.Ltarget:
    { xor32 r0, r0, r0 }

    .section .rodata
    .globl jt
jt:
    .byte .Ltarget - jt

#--- abs.s
    .section .data
    .globl p
p:
    .short ext
