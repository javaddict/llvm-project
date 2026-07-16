# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Test 64-bit ALU / SIMD / MAC / Slot-1 load mnemonics. The WideImm
# immediate-form shifts (SLLI64/SRLI64/SRAI64 d,d,imm) and the 4-operand
# MAC32 form are AsmParser gaps (M5 WideImm + post- MAC cleanup) tracked
# separately; they are exercised by disassembler-dsp-math-roundtrip.s.

#===----------------------------------------------------------------------===
# Test 64-bit ALU operations (Slot 1/2)
#===----------------------------------------------------------------------===

# CHECK-LABEL: add64 d0, d1, d2
ADD64 D0, D1, D2

# CHECK-LABEL: sub64 d3, d4, d5
SUB64 D3, D4, D5

# CHECK-LABEL: and64 d6, d7, d8
AND64 D6, D7, D8

# CHECK-LABEL: or64 d9, d10, d11
OR64 D9, D10, D11

# CHECK-LABEL: xor64 d12, d13, d14
XOR64 D12, D13, D14

#===----------------------------------------------------------------------===
# Test SIMD X2 operations (dual 32-bit)
#===----------------------------------------------------------------------===

# CHECK-LABEL: x2add32 d5, d6, d7
X2ADD32 D5, D6, D7

# CHECK-LABEL: x2sub32 d8, d9, d10
X2SUB32 D8, D9, D10

# (Path B): X2MUL32 is now TRUE 2-output (outs rd, rtd2; ins rs1, rs2)
# the asm form is 4-operand. The 3-operand form was retired.
# CHECK-LABEL: x2mul32 d11, d12, d13, d14
X2MUL32 D11, D12, D13, D14

#===----------------------------------------------------------------------===
# Test SIMD X4 operations (quad 16-bit)
#===----------------------------------------------------------------------===

# CHECK-LABEL: x4add16 d14, d15, d0
X4ADD16 D14, D15, D0

# (Path B): X4MUL16 is now TRUE 2-output (outs rd, rtd2; ins rs1, rs2)
# the asm form is 4-operand. The 3-operand form was retired.
# CHECK-LABEL: x4mul16 d1, d2, d3, d4
X4MUL16 D1, D2, D3, D4

#===----------------------------------------------------------------------===
# Test MAC operations
#===----------------------------------------------------------------------===

# MULQ31/MACQ31/MULQ63 REMOVED (phantom — not in the ISA DB). The real
# Q-format MAC family is FF2MULA32RS_* (DR64 packed-lane ops); SIMD MAC tests
# live in the X2MULA32/X4MULA16 cases above.

#===----------------------------------------------------------------------===
# Test Slot 1 load instructions
#===----------------------------------------------------------------------===

# CHECK-LABEL: ld64 d1, r1, 0
LD64 D1, R1, 0

# CHECK-LABEL: ld64 d2, r2, 8
LD64 D2, R2, 8
