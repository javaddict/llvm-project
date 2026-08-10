# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Comprehensive ALU instruction test covering all major ALU operations Based on slot0_alu_instruction_list.json.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 0f 32 04 32 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}18: 07 0f 52 86 ff ff 07 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}24: 07 cb 70 98 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}30: 07 0f aa 0b 19 00 00 00 00 00 00 00{{.*}}subi32
# CHECK: {{.*}}3c: 07 0f ca 00 fb ff 07 00 00 00 00 00{{.*}}subi32
# CHECK: {{.*}}48: 07 eb 10 32 00 00 00 00 00 00 00 00{{.*}}sub32s
# CHECK: {{.*}}54: 07 0b 41 65 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}60: 07 0f 74 88 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}6c: 07 0f 94 0a 80 7f 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}78: 07 2b b1 0c 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}84: 07 0f 18 02 40 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}90: 07 4b 31 54 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}9c: 07 0f 6c 07 20 00 00 00 00 00 00 00{{.*}}xori32
# CHECK: {{.*}}a8: 07 24 80 09 00 00 00 00 00 00 00 00{{.*}}not32
# CHECK: {{.*}}b4: 07 eb a1 cb 00 00 00 00 00 00 00 00{{.*}}sll32
# CHECK: {{.*}}c0: 07 06 04 01 04 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}cc: 07 06 24 03 1f 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}d8: 07 cb 41 65 00 00 00 00 00 00 00 00{{.*}}srl32
# CHECK: {{.*}}e4: 07 06 71 08 08 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}f0: 07 06 91 0a 10 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}fc: 07 8b b1 0c 00 00 00 00 00 00 00 00{{.*}}sra32
# CHECK: {{.*}}108: 07 06 12 02 01 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}114: 07 06 32 04 1f 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}120: 07 44 50 06 00 00 00 00 00 00 00 00{{.*}}move32
# CHECK: {{.*}}12c: 07 0a 72 00 2a 00 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}138: 07 0a 82 00 ff 0f 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}144: 07 24 91 0a 00 00 00 00 00 00 00 00{{.*}}abs32s
# CHECK: {{.*}}150: 07 0b b2 0c 00 00 00 00 00 00 00 00{{.*}}max32
# CHECK: {{.*}}15c: 07 2b 12 32 00 00 00 00 00 00 00 00{{.*}}maxu32
# CHECK: {{.*}}168: 07 4b 42 65 00 00 00 00 00 00 00 00{{.*}}min32
# CHECK: {{.*}}174: 07 6b 72 98 00 00 00 00 00 00 00 00{{.*}}minu32
# CHECK: {{.*}}180: 07 44 a1 0b 00 00 00 00 00 00 00 00{{.*}}neg32
# CHECK: {{.*}}18c: 07 64 c1 00 00 00 00 00 00 00 00 00{{.*}}neg32s
# CHECK: {{.*}}198: 07 6b 11 32 00 00 00 00 00 00 00 00{{.*}}brev32
# CHECK: {{.*}}1a4: 07 84 41 05 00 00 00 00 00 00 00 00{{.*}}nsa32
# CHECK: {{.*}}1b0: 07 a4 61 07 00 00 00 00 00 00 00 00{{.*}}nsau32
# CHECK: {{.*}}1bc: 07 c4 81 09 00 00 00 00 00 00 00 00{{.*}}popcount32
# CHECK: {{.*}}1c8: 07 8b a2 cb 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}1d4: 07 ab 02 21 00 00 00 00 00 00 00 00{{.*}}sltu32
# CHECK: {{.*}}1e0: 07 cb 32 54 00 00 00 00 00 00 00 00{{.*}}sle32
# CHECK: {{.*}}1ec: 07 eb 62 87 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK: {{.*}}1f8: 07 2b c3 10 00 00 00 00 00 00 00 00{{.*}}movt32
# CHECK: {{.*}}204: 07 0b 23 43 00 00 00 00 00 00 00 00{{.*}}movf32
# CHECK-NOT: <unknown>

# Comprehensive ALU instruction test covering all major ALU operations
# Based on slot0_alu_instruction_list.json
# NOTE: Instructions not in HaydnInstrInfo.td (SNE32, MUL32S, MULI32,
# DIV32, DIVU32, REM32, REMU32) have been removed.

#===----------------------------------------------------------------------===
# Arithmetic Operations
#===----------------------------------------------------------------------===


ADD32 R0, R1, R2

ADDI32 R3, R4, 100

ADDI32 R5, R6, -1

SUB32 R7, R8, R9

SUBI32 R10, R11, 50

SUBI32 R12, R0, -10

SUB32S R1, R2, R3

#===----------------------------------------------------------------------===
# Logical Operations
#===----------------------------------------------------------------------===

AND32 R4, R5, R6

ANDI32 R7, R8, 255

ANDI32 R9, R10, 0xFF00

OR32 R11, R12, R0

ORI32 R1, R2, 128

XOR32 R3, R4, R5

XORI32 R6, R7, 64

NOT32 R8, R9

#===----------------------------------------------------------------------===
# Shift Operations
#===----------------------------------------------------------------------===

SLL32 R10, R11, R12

SLLI32 R0, R1, 4

SLLI32 R2, R3, 31

SRL32 R4, R5, R6

SRLI32 R7, R8, 8

SRLI32 R9, R10, 16

SRA32 R11, R12, R0

SRAI32 R1, R2, 1

SRAI32 R3, R4, 31

#===----------------------------------------------------------------------===
# Move and Load Immediate
#===----------------------------------------------------------------------===

MOVE32 R5, R6

LUI R7, 42

LUI R8, 0xFFF

#===----------------------------------------------------------------------===
# DSP/Miscellaneous Operations
#===----------------------------------------------------------------------===

ABS32S R9, R10

MAX32 R11, R12, R0

MAXU32 R1, R2, R3

MIN32 R4, R5, R6

MINU32 R7, R8, R9

NEG32 R10, R11

NEG32S R12, R0

BREV32 R1, R2, R3

NSA32 R4, R5

NSAU32 R6, R7

POPCOUNT32 R8, R9

#===----------------------------------------------------------------------===
# Compare/Conditional Operations
#===----------------------------------------------------------------------===

SLT32 R10, R11, R12

SLTU32 R0, R1, R2

SLE32 R3, R4, R5

SEQ32 R6, R7, R8

# NOTE: SNE32 not defined — use SEQ32 + invert.

MOVT32 R12, R0, R1

MOVF32 R2, R3, R4

#===----------------------------------------------------------------------===
# Multiplication
#===----------------------------------------------------------------------===
# Scalar MUL32 removed : not in the ISA DB (the slot-0 scalar ALU has no
# multiply unit). Scalar s32 multiply now lowers to sext32t64 x2 + mul64.ll +
# move32_dr_l on the slot-1/2 DR64 unit, so there is no scalar GPR "mul" to
# round-trip here. SIMD X2MUL32 and the MUL64_* family are covered in their
# own MC files.
