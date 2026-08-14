# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Comprehensive instruction test for Haydn DSP. The WideImm immediate-form
# shifts (slli64/srli64/srai64) and the 4-operand MAC32 are AsmParser gaps
# (M5 + post- MAC cleanup) tracked separately.
# This test covers all major instruction categories

#===----------------------------------------------------------------------===
# Slot 0 ALU Instructions (32-bit scalar)
#===----------------------------------------------------------------------===

# Arithmetic
ADD32 R0, R1, R2
ADDI32 R3, R4, 100
SUB32 R5, R6, R7
SUBI32 R8, R9, -50

# Logical
AND32 R10, R11, R12
OR32 R0, R1, R2
XOR32 R3, R4, R5
NOT32 R6, R7

# Logical Immediate
ANDI32 R8, R9, 255
ORI32 R10, R11, 128
XORI32 R0, R1, 64

# Move and Load Immediate
MOVE32 R2, R3
LUI R4, 42

# Shifts (Immediate)
SRLI32 R5, R6, 4
SRAI32 R7, R8, 8
SLLI32 R9, R10, 16

# Shifts (Register)
SRL32 R11, R0, R1
SRA32 R2, R3, R4
SLL32 R5, R6, R7

# DSP/Miscellaneous
ABS32S R8, R9
MAX32 R10, R11, R12
MAXU32 R0, R1, R2
MIN32 R3, R4, R5
MINU32 R6, R7, R8
NEG32 R9, R10
NEG32S R11, R12
BREV32 R0, R1, R2
NSA32 R3, R4
NSAU32 R5, R6
POPCOUNT32 R7, R8

# Compare/Conditional Move
SLT32 R9, R10, R11
SLTU32 R12, R0, R1
SLE32 R2, R3, R4
SEQ32 R5, R6, R7
MOVT32 R8, R9, R10
MOVF32 R11, R12, R0

#===----------------------------------------------------------------------===
# Slot 0 Load/Store Instructions
#===----------------------------------------------------------------------===

# § 5.6 spellings; the immediate is an element index in a simm6 field, so the
# byte offsets these lines used to carry (8, 16, 24, 32) are divided by the
# access width rather than kept.
S_LW_WITH_IMM R1, R2, 0
S_LHWS_WITH_IMM R3, R4, 4
S_LBS_WITH_IMM R5, R6, 16
S_LHWU_WITH_IMM R7, R8, 12
S_LBU_WITH_IMM R9, R10, 31

S_SW_WITH_IMM R11, R12, 0
S_SHW_WITH_IMM R0, R1, 2
S_SB_WITH_IMM R2, R3, 2

#===----------------------------------------------------------------------===
# Branch Instructions
#===----------------------------------------------------------------------===

# Conditional branches (two-register)
branch_target:
BEQ R4, R5, branch_target
BNE R6, R7, branch_target
BGE R8, R9, branch_target
BGEU R10, R11, branch_target
BLT R12, R0, branch_target
BLTU R1, R2, branch_target

# Conditional branches (one-register)
BEQZ R3, branch_target
BNEZ R4, branch_target
BGEZ R5, branch_target
BLTZ R6, branch_target

#===----------------------------------------------------------------------===
# Jump/Link Instructions
#===----------------------------------------------------------------------===

# JAL R7, external_call # AsmParser gap: 2-operand JAL form not parsed (M5)

#===----------------------------------------------------------------------===
# Slot 1/2 ALU64/SIMD Instructions
#===----------------------------------------------------------------------===

# 64-bit operations
ADD64 D0, D1, D2
SUB64 D3, D4, D5
AND64 D6, D7, D8
OR64 D9, D10, D11
XOR64 D12, D13, D14

# 64-bit shifts — WideImm immediate form (slli64/srli64/srai64 d,d,imm)
# is an M5 AsmParser gap; not assembled here.

# SIMD X2 (dual 32-bit)
X2ADD32 D5, D6, D7
X2SUB32 D8, D9, D10
# (Path B): X2MUL32 is now TRUE 2-output — 4-operand asm form.
X2MUL32 D11, D12, D13, D14

# SIMD X4 (quad 16-bit)
X4ADD16 D14, D15, D0
# (Path B): X4MUL16 is now TRUE 2-output — 4-operand asm form.
X4MUL16 D1, D2, D3, D4

# MAC operations — 4-operand MAC32 removed post-; not assembled here.

# DR64 load
D_LDW_WITH_IMM D4, R5, 0

#===----------------------------------------------------------------------===
# System Instructions
#===----------------------------------------------------------------------===

CSRR R6, 0
CSRW 0, R7

# Verify all instructions are recognized
# CHECK: add32
# CHECK: addi32
# CHECK: sub32
# CHECK: subi32
# CHECK: and32
# CHECK: or32
# CHECK: xor32
# CHECK: not32
# CHECK: andi32
# CHECK: ori32
# CHECK: xori32
# CHECK: move32
# CHECK: lui
# CHECK: srli32
# CHECK: srai32
# CHECK: slli32
# CHECK: srl32
# CHECK: sra32
# CHECK: sll32
# CHECK: abs32s
# CHECK: max32
# CHECK: maxu32
# CHECK: min32
# CHECK: minu32
# CHECK: neg32
# CHECK: neg32s
# CHECK: brev32
# CHECK: nsa32
# CHECK: nsau32
# CHECK: popcount32
# CHECK: slt32
# CHECK: sltu32
# CHECK: sle32
# CHECK: seq32
# CHECK: movt32
# CHECK: movf32
# CHECK: s_lw_with_imm
# CHECK: s_lhws_with_imm
# CHECK: s_lbs_with_imm
# CHECK: s_lhwu_with_imm
# CHECK: s_lbu_with_imm
# CHECK: s_sw_with_imm
# CHECK: s_shw_with_imm
# CHECK: s_sb_with_imm
# CHECK: beq
# CHECK: bne
# CHECK: bge
# CHECK: bgeu
# CHECK: blt
# CHECK: bltu
# CHECK: beqz
# CHECK: bnez
# CHECK: bgez
# CHECK: bltz
# CHECK: add64
# CHECK: sub64
# CHECK: and64
# CHECK: or64
# CHECK: xor64
# CHECK: x2add32
# CHECK: x2sub32
# CHECK: x2mul32
# CHECK: x4add16
# CHECK: x4mul16
# Mulq31/macq31/mulq63 REMOVED (phantom — not in the ISA DB).
# CHECK: d_ldw_with_imm
# CHECK: csrr
# CHECK: csrw
