# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — 64-bit ALU / SIMD / 2-dest MAC / Slot-1 ld64 mnemonics.
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
# CHECK: {{.*}}78: 87 43 12 01 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK: {{.*}}84: 87 43 22 82 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK-NOT: <unknown>

# Immediate-form SLLI64/SRLI64/SRAI64 and 4-operand MAC32 AsmParser gaps
# are covered by disassembler-dsp-math-roundtrip.s.

#===----------------------------------------------------------------------===
# 64-bit ALU
#===----------------------------------------------------------------------===

ADD64 D0, D1, D2

SUB64 D3, D4, D5

AND64 D6, D7, D8

OR64 D9, D10, D11

XOR64 D12, D13, D14

#===----------------------------------------------------------------------===
# SIMD X2 (dual 32-bit)
#===----------------------------------------------------------------------===

X2ADD32 D5, D6, D7

X2SUB32 D8, D9, D10

# X2MUL32 is true 2-output: 4-operand asm form.
X2MUL32 D11, D12, D13, D14

#===----------------------------------------------------------------------===
# SIMD X4 (quad 16-bit)
#===----------------------------------------------------------------------===

X4ADD16 D14, D15, D0

X4MUL16 D1, D2, D3, D4

#===----------------------------------------------------------------------===
# Slot 1 loads
#===----------------------------------------------------------------------===

LD64 D1, R1, 0

LD64 D2, R2, 8
