# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Test all GPR / DR64 registers via parseable instructions. The WideImm
# immediate-form shift (slli64 d,d,imm) is an AsmParser gap tracked
# separately.

#===----------------------------------------------------------------------===
# Test all GPR registers R0-R12 (R13-R15 are reserved)
#===----------------------------------------------------------------------===

# CHECK-LABEL: add32 r0, r1, r2
ADD32 R0, R1, R2

# CHECK-LABEL: sub32 r4, r5, r6
SUB32 R4, R5, R6

# CHECK-LABEL: and32 r8, r9, r10
AND32 R8, R9, R10

# CHECK-LABEL: or32 r11, r12, r0
OR32 R11, R12, R0

#===----------------------------------------------------------------------===
# Test DR64 registers D0-D15
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
# Test mixed GPR and DR usage
#===----------------------------------------------------------------------===

# CHECK-LABEL: ld32 r0, r1, 0
LD32 R0, R1, 0

# CHECK-LABEL: ld64 d0, r2, 0
LD64 D0, R2, 0
