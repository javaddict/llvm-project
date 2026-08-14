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
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00 { nop; add64 d0, d1, d2 }
# CHECK: c: 07 0b 35 54 00 00 00 00 00 00 00 00 { nop; sub64 d3, d4, d5 }
# CHECK: {{.*}}18: 07 8b 66 87 00 00 00 00 00 00 00 00 { nop; and64 d6, d7, d8 }
# CHECK: {{.*}}24: 07 ab 96 ba 00 00 00 00 00 00 00 00 { nop; or64 d9, d10, d11 }
# CHECK: {{.*}}30: 07 cb c6 ed 00 00 00 00 00 00 00 00 { nop; xor64 d12, d13, d14 }
# CHECK: 3c: 07 6b 5a 06 00 00 00 00 00 00 00 00 { nop; x2sll32 d5, d6, r0 }
# CHECK: {{.*}}48: 07 4b 8a 09 00 00 00 00 00 00 00 00 { nop; x2srl32 d8, d9, r0 }
# CHECK: {{.*}}54: 07 0b ba 0c 00 00 00 00 00 00 00 00 { nop; x2sra32 d11, d12, r0 }
# CHECK: {{.*}}60: 87 43 02 07 00 00 00 00 00 00 00 00 { nop; ld64 d0, r7, 0 }
# CHECK: 6c: 87 43 12 88 00 00 00 00 00 00 00 00 { nop; ld64 d1, r8, 8 }
# CHECK: {{.*}}78: 87 43 f2 c0 03 00 00 00 00 00 00 00 { nop; ld64 d15, r0, -4 }
# CHECK: {{.*}}84: 87 43 2a 09 00 00 00 00 00 00 00 00 { nop; st64 d2, r9, 0 }
# CHECK: {{.*}}90: 87 43 3a 0a 03 00 00 00 00 00 00 00 { nop; st64 d3, r10, -16 }
# CHECK: 9c: 87 43 0a 81 00 00 00 00 00 00 00 00 { nop; st64 d0, r1, 8 }
# CHECK: a8: 87 43 13 42 00 00 00 00 00 00 00 00 { nop; ld32 r1, r2, 4 }
# CHECK: b4: 87 43 33 04 00 00 00 00 00 00 00 00 { nop; ld32 r3, r4, 0 }
# CHECK: c0: 07 2b 0a 21 00 00 00 00 00 00 00 00 { nop; x2sra32r d0, d1, r2 }
# CHECK: cc: 07 6b 0d 21 00 00 00 00 00 00 00 00 { nop; x4sll16 d0, d1, r2 }
# CHECK: d8: 07 0b 3d 54 00 00 00 00 00 00 00 00 { nop; x4sra16 d3, d4, r5 }
# CHECK: e4: 07 2b 6d 87 00 00 00 00 00 00 00 00 { nop; x4sra16r d6, d7, r8 }
# CHECK: f0: 07 4b 9d ba 00 00 00 00 00 00 00 00 { nop; x4srl16 d9, d10, r11 }
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
