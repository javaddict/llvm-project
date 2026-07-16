# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Round-trip test for DR64 and System instructions: asm → parse → print.
# The WideImm immediate-form shifts (slli64/srli64/srai64) and the
# 4-operand MAC32 form are AsmParser gaps (M5 WideImm + post- MAC
# cleanup) tracked separately.

# DR64 arithmetic operations
# CHECK: add64 d0, d1, d2
add64 d0, d1, d2

# CHECK: sub64 d3, d4, d5
sub64 d3, d4, d5

# CHECK: and64 d6, d7, d8
and64 d6, d7, d8

# CHECK: or64 d9, d10, d11
or64 d9, d10, d11

# CHECK: xor64 d12, d13, d14
xor64 d12, d13, d14

# SIMD X2 (dual 32-bit)
# CHECK: x2add32 d5, d6, d7
x2add32 d5, d6, d7

# CHECK: x2sub32 d8, d9, d10
x2sub32 d8, d9, d10

# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
# CHECK: x2mul32 d11, d12, d13, d14
x2mul32 d11, d12, d13, d14

# SIMD X4 (quad 16-bit)
# CHECK: x4add16 d14, d15, d0
x4add16 d14, d15, d0

# (Path B): X4MUL16 is now TRUE 2-output — 4-operand asm form.
# CHECK: x4mul16 d1, d2, d3, d4
x4mul16 d1, d2, d3, d4

# MAC operations
# Mulq31/macq31/mulq63 REMOVED (phantom — not in the ISA DB).
# The real Q-format MAC family is FF2MULA32RS_* (DR64 packed-lane ops).

# System instructions
# CHECK: csrr r4, 0
csrr r4, 0

# CHECK: csrw 0, r5
csrw 0, r5
