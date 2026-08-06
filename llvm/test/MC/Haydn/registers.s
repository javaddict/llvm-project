# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — GPR R0–R12 and DR64 D0–D15 parse and print via ordinary ops.

# Immediate-form slli64 is an AsmParser gap exercised elsewhere.

#===----------------------------------------------------------------------===
# GPR registers
#===----------------------------------------------------------------------===

ADD32 R0, R1, R2
# CHECK: add32 r0, r1, r2

SUB32 R4, R5, R6
# CHECK: sub32 r4, r5, r6

AND32 R8, R9, R10
# CHECK: and32 r8, r9, r10

OR32 R11, R12, R0
# CHECK: or32 r11, r12, r0

#===----------------------------------------------------------------------===
# DR64 registers
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
# Mixed GPR / DR loads
#===----------------------------------------------------------------------===

LD32 R0, R1, 0
# CHECK: ld32 r0, r1, 0

LD64 D0, R2, 0
# CHECK: ld64 d0, r2, 0
