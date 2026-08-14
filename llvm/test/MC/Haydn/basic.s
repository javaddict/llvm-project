# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Basic arithmetic instructions.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK-LABEL: <.text>:
# CHECK: {{.*}}0: 07 8b 00 21 00 00 00 00 00 00 00 00{{.*}}add32
# CHECK: {{.*}}c: 07 0f 02 01 15 00 00 00 00 00 00 00{{.*}}addi32
# CHECK: {{.*}}18: 07 cb 30 54 00 00 00 00 00 00 00 00{{.*}}sub32
# CHECK: {{.*}}24: 07 eb 60 87 00 00 00 00 00 00 00 00{{.*}}sub32s
# CHECK: {{.*}}30: 07 0b 91 ba 00 00 00 00 00 00 00 00{{.*}}and32
# CHECK: {{.*}}3c: 07 2b c1 10 00 00 00 00 00 00 00 00{{.*}}or32
# CHECK: {{.*}}48: 07 4b 21 43 00 00 00 00 00 00 00 00{{.*}}xor32
# CHECK: {{.*}}54: 07 0f 54 86 7f 00 00 00 00 00 00 00{{.*}}andi32
# CHECK: {{.*}}60: 07 0f 78 88 07 00 00 00 00 00 00 00{{.*}}ori32
# CHECK: {{.*}}6c: 07 0f 9c 8a 03 00 00 00 00 00 00 00{{.*}}xori32
# CHECK: {{.*}}78: 07 06 01 01 04 00 00 00 00 00 00 00{{.*}}srli32
# CHECK: {{.*}}84: 07 06 22 03 08 00 00 00 00 00 00 00{{.*}}srai32
# CHECK: {{.*}}90: 07 06 44 05 10 00 00 00 00 00 00 00{{.*}}slli32
# CHECK: {{.*}}9c: 07 cb 61 87 00 00 00 00 00 00 00 00{{.*}}srl32
# CHECK: {{.*}}a8: 07 8b 91 ba 00 00 00 00 00 00 00 00{{.*}}sra32
# CHECK: {{.*}}b4: 07 eb c1 10 00 00 00 00 00 00 00 00{{.*}}sll32
# CHECK: {{.*}}c0: 07 44 20 03 00 00 00 00 00 00 00 00{{.*}}move32
# CHECK: {{.*}}cc: 07 0a 42 00 2a 00 00 00 00 00 00 00{{.*}}lui
# CHECK: {{.*}}d8: 07 24 51 06 00 00 00 00 00 00 00 00{{.*}}abs32s
# CHECK: {{.*}}e4: 07 0b 72 98 00 00 00 00 00 00 00 00{{.*}}max32
# CHECK: {{.*}}f0: 07 4b a2 cb 00 00 00 00 00 00 00 00{{.*}}min32
# CHECK: {{.*}}fc: 07 44 01 01 00 00 00 00 00 00 00 00{{.*}}neg32
# CHECK: {{.*}}108: 07 8b 22 43 00 00 00 00 00 00 00 00{{.*}}slt32
# CHECK: {{.*}}114: 07 ab 52 76 00 00 00 00 00 00 00 00{{.*}}sltu32
# CHECK: {{.*}}120: 07 eb 82 a9 00 00 00 00 00 00 00 00{{.*}}seq32
# CHECK: {{.*}}12c: 87 43 03 01 00 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}138: 87 43 23 03 01 00 00 00 00 00 00 00{{.*}}ld32
# CHECK: {{.*}}144: 87 43 4b 05 00 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}150: 87 43 6b c7 03 00 00 00 00 00 00 00{{.*}}st32
# CHECK: {{.*}}15c: 87 43 84 09 00 00 00 00 00 00 00 00{{.*}}ld16
# CHECK: {{.*}}168: 87 43 a6 0b 00 00 00 00 00 00 00 00{{.*}}ld8
# CHECK: {{.*}}174: 87 43 c5 00 00 00 00 00 00 00 00 00{{.*}}ldu16
# CHECK: {{.*}}180: 87 43 17 02 00 00 00 00 00 00 00 00{{.*}}ldu8
# CHECK: {{.*}}18c: 87 43 3c 04 00 00 00 00 00 00 00 00{{.*}}st16
# CHECK: {{.*}}198: 87 43 5e 06 00 00 00 00 00 00 00 00{{.*}}st8
# CHECK: {{.*}}1a4: 07 0d 74 08 48 00 00 00 00 00 00 00{{.*}}beq
# CHECK: {{.*}}1b0: 07 0d 96 0a 3c 00 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}1bc: 07 0d b8 0c 30 00 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}1c8: 07 0d 0a 01 24 00 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}1d4: 07 0d 2c 03 18 00 00 00 00 00 00 00{{.*}}bgeu
# CHECK: {{.*}}1e0: 07 0d 4e 05 0c 00 00 00 00 00 00 00{{.*}}bltu
# CHECK: {{.*}}1ec: 07 0a 68 00 30 00 00 00 00 00 00 00{{.*}}beqz
# CHECK: {{.*}}1f8: 07 0a 7a 00 24 00 00 00 00 00 00 00{{.*}}bnez
# CHECK: {{.*}}204: 07 0a 8c 00 18 00 00 00 00 00 00 00{{.*}}bgez
# CHECK: {{.*}}210: 07 0a 9e 00 0c 00 00 00 00 00 00 00{{.*}}bltz
# CHECK: {{.*}}21c: 07 0e a8 00 0c 00 00 00 00 00 00 00{{.*}}jal
# CHECK: {{.*}}228: 07 0d b2 0c 0c 00 00 00 00 00 00 00{{.*}}jalr
# CHECK-NOT: <unknown>

# Basic arithmetic instructions

ADD32 R0, R1, R2

ADDI32 R0, R1, 42

SUB32 R3, R4, R5

SUB32S R6, R7, R8

# Logical instructions
AND32 R9, R10, R11

OR32 R12, R0, R1

XOR32 R2, R3, R4

# Logical immediate instructions
ANDI32 R5, R6, 255

ORI32 R7, R8, 15

XORI32 R9, R10, 7

# Shift instructions (immediate)
SRLI32 R0, R1, 4

SRAI32 R2, R3, 8

SLLI32 R4, R5, 16

# Shift instructions (register)
SRL32 R6, R7, R8

SRA32 R9, R10, R11

SLL32 R12, R0, R1

# Move and load immediate
MOVE32 R2, R3

LUI R4, 42

# DSP/miscellaneous instructions
ABS32S R5, R6

MAX32 R7, R8, R9

MIN32 R10, R11, R12

NEG32 R0, R1

# Compare instructions
SLT32 R2, R3, R4

SLTU32 R5, R6, R7

SEQ32 R8, R9, R10

# Load/store instructions
LD32 R0, R1, 0

LD32 R2, R3, 16

ST32 R4, R5, 0

ST32 R6, R7, -4

# Load/store size variants
LD16 R8, R9, 0

LD8 R10, R11, 0

LDU16 R12, R0, 0

LDU8 R1, R2, 0

ST16 R3, R4, 0

ST8 R5, R6, 0

# Branch instructions
BEQ R7, R8, .Ltarget

BNE R9, R10, .Ltarget

BGE R11, R12, .Ltarget

BLT R0, R1, .Ltarget

BGEU R2, R3, .Ltarget

BLTU R4, R5, .Ltarget

.Ltarget:
BEQZ R6, .Ltarget2

BNEZ R7, .Ltarget2

BGEZ R8, .Ltarget2

BLTZ R9, .Ltarget2

.Ltarget2:

# Jump and link instructions
JAL R10, foo

JALR R11, R12, bar

foo:
bar:
