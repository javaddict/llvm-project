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

# The relocation OFFSET is not an invariant and must not be read as one: it
# is `bundle_start + the entry's byte base`, so it moves whenever the packer
# puts the instruction in a different entry (§ 5.4, § 5.12). What IS invariant
# and what these two numbers are checked against:
#
#   3-entry entry0 = bit[36:6]  -> byte 0    entry1 = bit[67:37] -> byte 4
#                    entry2 = bit[94:68] -> byte 8
#   2-entry entry0 = bit[50:6]  -> byte 0    entry1 = bit[91:51] -> byte 6
#
# lui sits in the 3-entry bundle at 0x0, entry2  -> 0x0 + 8  = 0x8
# addi32 sits in the 2-entry bundle at 0xc, entry1 -> 0xc + 6 = 0x12
#
# If these move, check the disassembly for which entry each landed in before
# assuming the relocation is wrong.
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x8 R_HAYDN_HI12 high_sym 0x0
# RELOCS-NEXT:     0x12 R_HAYDN_LO20 high_sym 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

# .rodata is pinned by the RUN line, so the linked VALUES are unaffected by
# the parcel width: HI12 = (0x7BC00000 + 0x80000) >> 20 = 0x7BC = 1980 and
# LO20 = 0x7BC00000 - (1980 << 20) = 0. Only the address of the second parcel
# moves, 0x10010 -> 0x1000c, because a bundle is 12 bytes now.
# ELF: <_start>:
# ELF: 10000: {{.*}} lui{{.*}} r3, 1980
# ELF: 1000c: {{.*}} addi32{{.*}} r3, r3, 0

    .section .text
    .globl _start
    .type _start, @function
_start:
    lui      R3, high_sym
    addi32   R3, R3, high_sym
    .size _start, .-_start

    .section .rodata
    .globl high_sym
    .type high_sym, @object
high_sym:
    .long 0x12345678
    .size high_sym, .-high_sym
