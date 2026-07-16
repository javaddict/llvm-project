# REQUIRES: haydn-registered-target
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld -m elf32haydn %t.o -o %t \
# RUN:   --section-start=.text=0x10000 --section-start=.rodata=0x7BC00000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck --check-prefix=ELF %s

# REGRESSION TEST (/ post- residual P0): Bundle128 LUI_S0_FLEX has a
# full imm12 at LoWord bits[15:4] (HaydnFU_ALU32_S0_I12). The HI12 reloc row
# previously used Mode-0 residual FieldSize=5 (bits[8:4] only), so any
# HI12 > 31 failed range-check / truncated — latent for bare-metal HI12<=31
# but fatal for large absolute addresses (≳32MB).
#
# Address math (bit 31 CLEAR so 32-bit absolute VAs do not sign-extend into
# the 64-bit reloc value — a separate pre-existing concern for VA>=0x80000000):
# high_sym @ 0x7BC00000
# HI12 = (0x7BC00000 + 0x80000) >> 20 = 0x7BC80000 >> 20 = 0x7BC (= 1980)
# LO20 = 0x7BC00000 - (0x7BC << 20) = 0
# Pre- FieldSize=5 rejects Hi=0x7BC ("does not fit in the LUI ext field").
# Post- the linked pair is `lui r3, 1980` + `addi32{{(_w)?}} r3, r3, 0`.
# 0x7BC sets bits[10:2] of imm12 — needs FieldSize>=11, so FieldSize=5 cannot
# pass even by partial truncation luck.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_HI12 high_sym 0x0
# RELOCS-NEXT:     0x10 R_HAYDN_LO20 high_sym 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

# ELF: <_start>:
# ELF: 10000: {{.*}} lui{{.*}} r3, 1980
# ELF: 10010: {{.*}} addi32{{(_w)?}}{{.*}} r3, r3,

    .section .text
    .globl _start
    .type _start, @function
_start:
    lui      R3, high_sym
    addi32_w R3, R3, high_sym
    .size _start, .-_start

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0x12345678
    .size high_sym, .-high_sym
