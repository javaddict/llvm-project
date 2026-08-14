# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — GPR R0–R12 and DR64 D0–D15 parse and print via ordinary ops.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 cb 40 65 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}18: 07 0b 81 a9 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}24: 07 2b b1 0c 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}30: 07 0b 04 21 00 00 00 00 00 00 00 00{{.*}}add64
# CHECK: {{.*}}3c: 07 0b 35 54 00 00 00 00 00 00 00 00{{.*}}sub64
# CHECK: {{.*}}48: 07 8b 66 87 00 00 00 00 00 00 00 00{{.*}}and64
# CHECK: {{.*}}54: 07 ab 96 ba 00 00 00 00 00 00 00 00{{.*}}or64
# CHECK: {{.*}}60: 07 cb c6 ed 00 00 00 00 00 00 00 00{{.*}}xor64
# CHECK: {{.*}}6c: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}78: 87 43 02 02 00 00 00 00 00 00 00 00{{.*}}ld64
# CHECK-NOT: <unknown>

# Immediate-form slli64 is an AsmParser gap exercised elsewhere.

#===----------------------------------------------------------------------===
# GPR registers
#===----------------------------------------------------------------------===

ADD32 R0, R1, R2

SUB32 R4, R5, R6

AND32 R8, R9, R10

OR32 R11, R12, R0

#===----------------------------------------------------------------------===
# DR64 registers
#===----------------------------------------------------------------------===

ADD64 D0, D1, D2

SUB64 D3, D4, D5

AND64 D6, D7, D8

OR64 D9, D10, D11

XOR64 D12, D13, D14

#===----------------------------------------------------------------------===
# Mixed GPR / DR loads
#===----------------------------------------------------------------------===

LD32 R0, R1, 0

LD64 D0, R2, 0
