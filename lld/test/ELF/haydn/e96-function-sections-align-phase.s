# REQUIRES: haydn
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf b.s -o b.o
# RUN: ld.lld a.o b.o -o out --section-start=.text=0x10000
# RUN: llvm-nm out | FileCheck %s --check-prefix=NM
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf out | \
# RUN:   FileCheck %s --check-prefix=DIS
# RUN: llvm-readobj -S out | FileCheck %s --check-prefix=SEC

# Product images use -ffunction-sections. A 12-byte first function then a
# 16-aligned neighbor used to start at +16 (4 mod EncodedBytes). BundleSim
# then rejected the JAL ("direct control target is not an exact code record").
# LLD grows exec inputs to lcm(maxAlign, 12)=48 so the 16-aligned start stays
# on the .text parcel phase (AIE Align(16) is already 2^n;
# AIETargetELFStreamer.cpp:73-81). Idle parcels fill the grown tail.

# NM: {{0+}}10000 T _start
# NM: {{0+}}10030 T aligned16

# SEC: Name: .text
# SEC: Address: 0x10000
# SEC: Size: 96

# DIS-LABEL: <_start>:
# DIS:    10000: {{.*}}jal{{.*}}lr, 48
# DIS-LABEL: <aligned16>:
# DIS:    10030:

#--- a.s
        .section .text._start,"ax",@progbits
        .globl _start
        .type _start, @function
_start:
        { jal lr, aligned16; nop; nop }
        .size _start, .-_start

#--- b.s
        .section .text.aligned16,"ax",@progbits
        .p2align 4
        .globl aligned16
        .type aligned16, @function
aligned16:
        { nop; nop; jalr r0, lr, 0 }
        .size aligned16, .-aligned16
