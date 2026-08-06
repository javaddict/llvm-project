# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — DR64 ALU/load/store/SIMD-shift asm→obj→objdump round-trip.

# Covers DR64-register instructions that have real encodings and parse via the
# current AsmParser. WideImm immediate shifts (SLLI64/SRLI64/SRAI64) remain an
# AsmParser gap and are exercised by disassembler-dsp-math-roundtrip.s via
# decoder paths.
#
# Each line must survive Format E encode and disassemble to the same mnemonic
# family. Assemble-to-/dev/null alone is not the contract.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00 { add64 d0, d1, d2; nop }
# CHECK: c: 07 0b 35 54 00 00 00 00 00 00 00 00 { sub64 d3, d4, d5; nop }
# CHECK: {{.*}}18: 07 8b 66 87 00 00 00 00 00 00 00 00 { and64 d6, d7, d8; nop }
# CHECK: {{.*}}24: 07 ab 96 ba 00 00 00 00 00 00 00 00 { or64 d9, d10, d11; nop }
# CHECK: {{.*}}30: 07 cb c6 ed 00 00 00 00 00 00 00 00 { xor64 d12, d13, d14; nop }
# CHECK: 3c: 07 6b 5a 06 00 00 00 00 00 00 00 00 { x2sll32 d5, d6, r0; nop }
# CHECK: {{.*}}48: 07 4b 8a 09 00 00 00 00 00 00 00 00 { x2srl32 d8, d9, r0; nop }
# CHECK: {{.*}}54: 07 0b ba 0c 00 00 00 00 00 00 00 00 { x2sra32 d11, d12, r0; nop }
# CHECK: {{.*}}60: 87 43 02 07 00 00 00 00 00 00 00 00 { d_ldw_with_imm d0, r7, 0; nop }
# CHECK: 6c: 87 43 12 88 00 00 00 00 00 00 00 00 { d_ldw_with_imm d1, r8, 8; nop }
# CHECK: {{.*}}78: 87 43 f2 c0 03 00 00 00 00 00 00 00 { d_ldw_with_imm d15, r0, -4; nop }
# CHECK: {{.*}}84: 87 43 2a 09 00 00 00 00 00 00 00 00 { d_sdw_with_imm d2, r9, 0; nop }
# CHECK: {{.*}}90: 87 43 3a 0a 03 00 00 00 00 00 00 00 { d_sdw_with_imm d3, r10, -16; nop }
# CHECK: 9c: 87 43 0a 81 00 00 00 00 00 00 00 00 { d_sdw_with_imm d0, r1, 8; nop }
# CHECK: a8: 87 43 13 42 00 00 00 00 00 00 00 00 { s_lw_with_imm r1, r2, 4; nop }
# CHECK: b4: 87 43 33 04 00 00 00 00 00 00 00 00 { s_lw_with_imm r3, r4, 0; nop }
# CHECK: c0: 07 2b 0a 21 00 00 00 00 00 00 00 00 { x2sra32r d0, d1, r2; nop }
# CHECK: cc: 07 6b 0d 21 00 00 00 00 00 00 00 00 { x4sll16 d0, d1, r2; nop }
# CHECK: d8: 07 0b 3d 54 00 00 00 00 00 00 00 00 { x4sra16 d3, d4, r5; nop }
# CHECK: e4: 07 2b 6d 87 00 00 00 00 00 00 00 00 { x4sra16r d6, d7, r8; nop }
# CHECK: f0: 07 4b 9d ba 00 00 00 00 00 00 00 00 { x4srl16 d9, d10, r11; nop }
# CHECK-NOT: <?>
# CHECK-NOT: <unknown>

#===----------------------------------------------------------------------===
# 64-bit ALU Arithmetic
#===----------------------------------------------------------------------===

add64 d0, d1, d2
sub64 d3, d4, d5
and64 d6, d7, d8
or64 d9, d10, d11
xor64 d12, d13, d14

#===----------------------------------------------------------------------===
# SIMD X2 Shift (dual 32-bit)
#===----------------------------------------------------------------------===

x2sll32 d5, d6, r0
x2srl32 d8, d9, r0
x2sra32 d11, d12, r0

#===----------------------------------------------------------------------===
# DR64 Load/Store
#===----------------------------------------------------------------------===

ld64 d0, r7, 0
ld64 d1, r8, 8
ld64 d15, r0, 1020
st64 d2, r9, 0
st64 d3, r10, -16
st64 d0, r1, 8

#===----------------------------------------------------------------------===
# Slot scalar load
#===----------------------------------------------------------------------===

ld32 r1, r2, 4
ld32 r3, r4, 0

#===----------------------------------------------------------------------===
# X2SRA32R and X4 shifts
#===----------------------------------------------------------------------===

x2sra32r d0, d1, r2
x4sll16 d0, d1, r2
x4sra16 d3, d4, r5
x4sra16r d6, d7, r8
x4srl16 d9, d10, r11
