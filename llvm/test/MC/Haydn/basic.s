# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Basic arithmetic instructions
ADD32 R0, R1, R2
# CHECK: add32 r0, r1, r2

ADDI32 R0, R1, 42
# CHECK: addi32 r0, r1, 42

SUB32 R3, R4, R5
# CHECK: sub32 r3, r4, r5

SUB32S R6, R7, R8
# CHECK: sub32s r6, r7, r8

# Logical instructions
AND32 R9, R10, R11
# CHECK: and32 r9, r10, r11

OR32 R12, R0, R1
# CHECK: or32 r12, r0, r1

XOR32 R2, R3, R4
# CHECK: xor32 r2, r3, r4

# Logical immediate instructions
ANDI32 R5, R6, 255
# CHECK: andi32 r5, r6, 255

ORI32 R7, R8, 15
# CHECK: ori32 r7, r8, 15

XORI32 R9, R10, 7
# CHECK: xori32 r9, r10, 7

# Shift instructions (immediate)
SRLI32 R0, R1, 4
# CHECK: srli32 r0, r1, 4

SRAI32 R2, R3, 8
# CHECK: srai32 r2, r3, 8

SLLI32 R4, R5, 16
# CHECK: slli32 r4, r5, 16

# Shift instructions (register)
SRL32 R6, R7, R8
# CHECK: srl32 r6, r7, r8

SRA32 R9, R10, R11
# CHECK: sra32 r9, r10, r11

SLL32 R12, R0, R1
# CHECK: sll32 r12, r0, r1

# Move and load immediate
MOVE32 R2, R3
# CHECK: move32 r2, r3

LUI R4, 42
# CHECK: lui r4, 42

# DSP/miscellaneous instructions
ABS32S R5, R6
# CHECK: abs32s r5, r6

MAX32 R7, R8, R9
# CHECK: max32 r7, r8, r9

MIN32 R10, R11, R12
# CHECK: min32 r10, r11, r12

NEG32 R0, R1
# CHECK: neg32 r0, r1

# Compare instructions
SLT32 R2, R3, R4
# CHECK: slt32 r2, r3, r4

SLTU32 R5, R6, R7
# CHECK: sltu32 r5, r6, r7

SEQ32 R8, R9, R10
# CHECK: seq32 r8, r9, r10

# Load/store instructions. The § 5.6 spellings, and the immediate is an
# element index in a simm6 field rather than a byte offset.
S_LW_WITH_IMM R0, R1, 0
# CHECK: s_lw_with_imm r0, r1, 0

S_LW_WITH_IMM R2, R3, 4
# CHECK: s_lw_with_imm r2, r3, 4

S_SW_WITH_IMM R4, R5, 0
# CHECK: s_sw_with_imm r4, r5, 0

S_SW_WITH_IMM R6, R7, -1
# CHECK: s_sw_with_imm r6, r7, -1

# Load/store size variants
S_LHWS_WITH_IMM R8, R9, 0
# CHECK: s_lhws_with_imm r8, r9, 0

S_LBS_WITH_IMM R10, R11, 0
# CHECK: s_lbs_with_imm r10, r11, 0

S_LHWU_WITH_IMM R12, R0, 0
# CHECK: s_lhwu_with_imm r12, r0, 0

S_LBU_WITH_IMM R1, R2, 0
# CHECK: s_lbu_with_imm r1, r2, 0

S_SHW_WITH_IMM R3, R4, 0
# CHECK: s_shw_with_imm r3, r4, 0

S_SB_WITH_IMM R5, R6, 0
# CHECK: s_sb_with_imm r5, r6, 0

# Branch instructions
BEQ R7, R8, .Ltarget
# CHECK: beq r7, r8, .Ltarget

BNE R9, R10, .Ltarget
# CHECK: bne r9, r10, .Ltarget

BGE R11, R12, .Ltarget
# CHECK: bge r11, r12, .Ltarget

BLT R0, R1, .Ltarget
# CHECK: blt r0, r1, .Ltarget

BGEU R2, R3, .Ltarget
# CHECK: bgeu r2, r3, .Ltarget

BLTU R4, R5, .Ltarget
# CHECK: bltu r4, r5, .Ltarget

.Ltarget:
BEQZ R6, .Ltarget2
# CHECK: beqz r6, .Ltarget2

BNEZ R7, .Ltarget2
# CHECK: bnez r7, .Ltarget2

BGEZ R8, .Ltarget2
# CHECK: bgez r8, .Ltarget2

BLTZ R9, .Ltarget2
# CHECK: bltz r9, .Ltarget2

.Ltarget2:

# Jump and link instructions
JAL R10, foo
# CHECK: jal r10, foo

JALR R11, R12, bar
# CHECK: jalr r11, r12, bar

foo:
bar:
