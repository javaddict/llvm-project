# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — 64-bit ALU / SIMD / 2-dest MAC / Slot-1 ld64 mnemonics.

# Immediate-form SLLI64/SRLI64/SRAI64 and 4-operand MAC32 AsmParser gaps
# are covered by disassembler-dsp-math-roundtrip.s.

#===----------------------------------------------------------------------===
# 64-bit ALU
#===----------------------------------------------------------------------===

ADD64 D0, D1, D2
# CHECK: add64 d0, d1, d2

SUB64 D3, D4, D5
# CHECK: sub64 d3, d4, d5

AND64 D6, D7, D8
# CHECK: and64 d6, d7, d8

OR64 D9, D10, D11
# CHECK: or64 d9, d10, d11

XOR64 D12, D13, D14
# CHECK: xor64 d12, d13, d14

#===----------------------------------------------------------------------===
# SIMD X2 (dual 32-bit)
#===----------------------------------------------------------------------===

X2ADD32 D5, D6, D7
# CHECK: x2add32 d5, d6, d7

X2SUB32 D8, D9, D10
# CHECK: x2sub32 d8, d9, d10

# X2MUL32 is true 2-output: 4-operand asm form.
X2MUL32 D11, D12, D13, D14
# CHECK: x2mul32 d11, d12, d13, d14

#===----------------------------------------------------------------------===
# SIMD X4 (quad 16-bit)
#===----------------------------------------------------------------------===

X4ADD16 D14, D15, D0
# CHECK: x4add16 d14, d15, d0

X4MUL16 D1, D2, D3, D4
# CHECK: x4mul16 d1, d2, d3, d4

#===----------------------------------------------------------------------===
# Slot 1 loads
#===----------------------------------------------------------------------===

LD64 D1, R1, 0
# CHECK: ld64 d1, r1, 0

LD64 D2, R2, 8
# CHECK: ld64 d2, r2, 8
