# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Phase-2 ALU/LS/DR64/SIMD encode→obj→disasm round-trip.
# Converted from parse-only/show-encoding to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).
# Fail-closed: no positive ar_sel=2/3, all-zero product-NOP, or golden-unspecified branch-scale invent.

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 cb 30 54 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}18: 07 0b 61 87 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}24: 07 2b 91 ba 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}30: 07 4b c1 10 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}3c: 07 8b 02 21 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}48: 07 eb 02 21 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK: {{.*}}54: 07 44 00 01 00 00 00 00 00 00 00 00{{.*}}move32
# CHECK: {{.*}}60: 07 24 00 01 00 00 00 00 00 00 00 00{{.*}}not32
# CHECK: {{.*}}6c: 07 44 01 01 00 00 00 00 00 00 00 00{{.*}}neg32
# CHECK: {{.*}}78: 07 04 01 01 00 00 00 00 00 00 00 00{{.*}}abs32
# CHECK: {{.*}}84: 07 0f 02 01 15 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}90: 07 06 04 01 04 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}9c: 07 06 01 01 08 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}a8: 07 06 02 01 10 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}b4: 07 0a 02 00 00 04 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}c0: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}cc: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}d8: 87 43 4b 05 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}e4: 87 43 6b c7 03 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}f0: 07 0b 04 21 00 00 00 00 00 00 00 00{{.*}}add64
# CHECK: {{.*}}fc: 07 0b 35 54 00 00 00 00 00 00 00 00{{.*}}sub64
# CHECK: {{.*}}108: 07 8b 66 87 00 00 00 00 00 00 00 00{{.*}}and64
# CHECK: {{.*}}114: 07 ab 06 21 00 00 00 00 00 00 00 00{{.*}}or64
# CHECK: {{.*}}120: 47 02 01 21 03 00 00 00 00 00 00 00{{.*}}x2mul32
# CHECK: {{.*}}12c: 47 02 0c 21 03 00 00 00 00 00 00 00{{.*}}x4mul16
# CHECK: {{.*}}138: 07 0b 0c 21 00 00 00 00 00 00 00 00{{.*}}x4add16
# CHECK-LABEL: <target_branch>:
# CHECK: {{.*}}144: 07 0d 04 01 00 00 00 00 00 00 00 00{{.*}}beq
# CHECK-LABEL: <target_branch2>:
# CHECK: {{.*}}150: 07 0d 06 01 00 00 00 00 00 00 00 00{{.*}}bne
# CHECK-LABEL: <target_jal>:
# CHECK: {{.*}}15c: 07 0e 08 00 00 00 00 00 00 00 00 00{{.*}}jal

ADD32 R0, R1, R2

SUB32 R3, R4, R5

AND32 R6, R7, R8

OR32 R9, R10, R11

XOR32 R12, R0, R1

SLT32 R0, R1, R2

SEQ32 R0, R1, R2

# mul32 removed : not in the ISA DB; scalar s32 mul lowers via the
# slot-1/2 DR64 unit (sext32t64 + mul64.ll + move32_dr_l). No scalar GPR mul.

MOVE32 R0, R1

NOT32 R0, R1

NEG32 R0, R1

ABS32 R0, R1

#===----------------------------------------------------------------------===
# Test ALU32 immediate instructions (s0, FU=ALU32, RI format)
#===----------------------------------------------------------------------===

ADDI32 R0, R1, 42

SLLI32 R0, R1, 4

SRLI32 R0, R1, 8

SRAI32 R0, R1, 16

LUI R0, 1024

#===----------------------------------------------------------------------===
# Test Load/Store instructions (s0, FU=LS)
#===----------------------------------------------------------------------===

LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

ST32 R6, R7, -4

#===----------------------------------------------------------------------===
# Test ALU64 instructions (s1/s2, FU=ALU64)
# NOTE: move64, neg64, not64 have no MC encoding (isPseudo) — excluded.
#===----------------------------------------------------------------------===

ADD64 D0, D1, D2

SUB64 D3, D4, D5

AND64 D6, D7, D8

OR64 D0, D1, D2

#===----------------------------------------------------------------------===
# Test MAC instructions (s1/s2, FU=MAC)
#===----------------------------------------------------------------------===

# (Path B): X2MUL32 / X4MUL16 are now TRUE 2-output SIMD-MAC
# (outs DR64:$rd, DR64:$rtd2; ins DR64:$rs1, DR64:$rs2) — the asm form is
# 4-operand. The 3-operand form was retired.
X2MUL32 D0, D1, D2, D3

X4MUL16 D0, D1, D2, D3

X4ADD16 D0, D1, D2

#===----------------------------------------------------------------------===
# Test branch instructions (slot 0 only)
#===----------------------------------------------------------------------===

target_branch:
BEQ R0, R1, target_branch

target_branch2:
BNE R0, R1, target_branch2

target_jal:
JAL R0, target_jal
