# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Round-trip test for DR64 and System instructions: asm → parse → print.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00{{.*}}add64
# CHECK: {{.*}}c: 07 0b 35 54 00 00 00 00 00 00 00 00{{.*}}sub64
# CHECK: {{.*}}18: 07 8b 66 87 00 00 00 00 00 00 00 00{{.*}}and64
# CHECK: {{.*}}24: 07 ab 96 ba 00 00 00 00 00 00 00 00{{.*}}or64
# CHECK: {{.*}}30: 07 cb c6 ed 00 00 00 00 00 00 00 00{{.*}}xor64
# CHECK: {{.*}}3c: 07 0b 58 76 00 00 00 00 00 00 00 00{{.*}}x2add32
# CHECK: {{.*}}48: 07 8b 89 a9 00 00 00 00 00 00 00 00{{.*}}x2sub32
# CHECK: {{.*}}54: 47 02 b1 dc 0e 00 00 00 00 00 00 00{{.*}}x2mul32
# CHECK: {{.*}}60: 07 0b ec 0f 00 00 00 00 00 00 00 00{{.*}}x4add16
# CHECK: {{.*}}6c: 47 02 1c 32 04 00 00 00 00 00 00 00{{.*}}x4mul16
# CHECK: {{.*}}78: 07 03 48 00 00 00 00 00 00 00 00 00{{.*}}csrr
# CHECK: {{.*}}84: 07 03 5a 00 00 00 00 00 00 00 00 00{{.*}}csrw
# CHECK-NOT: <unknown>

# Round-trip test for DR64 and System instructions: asm → parse → print.
# The WideImm immediate-form shifts (slli64/srli64/srai64) and the
# 4-operand MAC32 form are AsmParser gaps (M5 WideImm + post- MAC
# cleanup) tracked separately.

# DR64 arithmetic operations

add64 d0, d1, d2

sub64 d3, d4, d5

and64 d6, d7, d8

or64 d9, d10, d11

xor64 d12, d13, d14

# SIMD X2 (dual 32-bit)
x2add32 d5, d6, d7

x2sub32 d8, d9, d10

# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
x2mul32 d11, d12, d13, d14

# SIMD X4 (quad 16-bit)
x4add16 d14, d15, d0

# (Path B): X4MUL16 is now TRUE 2-output — 4-operand asm form.
x4mul16 d1, d2, d3, d4

# MAC operations
# Mulq31/macq31/mulq63 REMOVED (phantom — not in the ISA DB).
# The real Q-format MAC family is FF2MULA32RS_* (DR64 packed-lane ops).

# System instructions
csrr r4, 0

csrw 0, r5
