# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — 32-bit-ish ALU/LS/branch encode→obj→disasm regression (Format E parcels).
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 cb 30 54 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}18: 07 0b 61 87 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}24: 07 2b 91 ba 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}30: 07 4b c1 10 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}3c: 07 0f 02 01 15 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}48: 07 0f 44 85 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}54: 07 0f 68 87 07 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}60: 07 06 a1 0b 04 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}6c: 07 06 c2 00 08 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}78: 07 06 14 02 10 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}84: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}90: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}9c: 87 43 4b 05 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}a8: 07 0d 84 09 0c 00 00 00 00 00 00 00{{.*}}beq
# CHECK-LABEL: <target_32>:
# CHECK: {{.*}}b4: 07 0d a6 0b 0c 00 00 00 00 00 00 00{{.*}}bne

ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

#===----------------------------------------------------------------------===
# ALU I-type (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

ANDI32 R4, R5, 255

ORI32 R6, R7, 15

#===----------------------------------------------------------------------===
# Shift immediate (Mode-0 s0 ALU32 sub-row, 8 bytes)
#===----------------------------------------------------------------------===

SRLI32 R10, R11, 4

SRAI32 R12, R0, 8

SLLI32 R1, R2, 16

#===----------------------------------------------------------------------===
# Load/Store (legacy FmtLS, 4 bytes — : asm-parse routes to the generic
# simm16 variant; the Mode-0 LD32_M0S0LS/ST32_M0S0LS ×4-scaled variants are
# finalizer emit targets for CodeGen, not asm-parse targets)
#===----------------------------------------------------------------------===

LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

#===----------------------------------------------------------------------===
# Branch (FmtBr, 4 bytes)
#===----------------------------------------------------------------------===

BEQ R8, R9, target_32
target_32:

BNE R10, R11, target_32b
target_32b:
