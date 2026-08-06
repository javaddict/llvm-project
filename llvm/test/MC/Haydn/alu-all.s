# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — Comprehensive ALU instruction test covering all major ALU operations Based on slot0_alu_instruction_list.json.

# Comprehensive ALU instruction test covering all major ALU operations
# Based on slot0_alu_instruction_list.json
# NOTE: Instructions not in HaydnInstrInfo.td (SNE32, MUL32S, MULI32,
# DIV32, DIVU32, REM32, REMU32) have been removed.

#===----------------------------------------------------------------------===
# Arithmetic Operations
#===----------------------------------------------------------------------===

# CHECK: add32 r0, r1, r2

ADD32 R0, R1, R2

# CHECK: addi32 r3, r4, 100
ADDI32 R3, R4, 100

# CHECK: addi32 r5, r6, -1
ADDI32 R5, R6, -1

# CHECK: sub32 r7, r8, r9
SUB32 R7, R8, R9

# CHECK: subi32 r10, r11, 50
SUBI32 R10, R11, 50

# CHECK: subi32 r12, r0, -10
SUBI32 R12, R0, -10

# CHECK: sub32s r1, r2, r3
SUB32S R1, R2, R3

#===----------------------------------------------------------------------===
# Logical Operations
#===----------------------------------------------------------------------===

# CHECK: and32 r4, r5, r6
AND32 R4, R5, R6

# CHECK: andi32 r7, r8, 255
ANDI32 R7, R8, 255

# CHECK: andi32 r9, r10, 65280
ANDI32 R9, R10, 0xFF00

# CHECK: or32 r11, r12, r0
OR32 R11, R12, R0

# CHECK: ori32 r1, r2, 128
ORI32 R1, R2, 128

# CHECK: xor32 r3, r4, r5
XOR32 R3, R4, R5

# CHECK: xori32 r6, r7, 64
XORI32 R6, R7, 64

# CHECK: not32 r8, r9
NOT32 R8, R9

#===----------------------------------------------------------------------===
# Shift Operations
#===----------------------------------------------------------------------===

# CHECK: sll32 r10, r11, r12
SLL32 R10, R11, R12

# CHECK: slli32 r0, r1, 4
SLLI32 R0, R1, 4

# CHECK: slli32 r2, r3, 31
SLLI32 R2, R3, 31

# CHECK: srl32 r4, r5, r6
SRL32 R4, R5, R6

# CHECK: srli32 r7, r8, 8
SRLI32 R7, R8, 8

# CHECK: srli32 r9, r10, 16
SRLI32 R9, R10, 16

# CHECK: sra32 r11, r12, r0
SRA32 R11, R12, R0

# CHECK: srai32 r1, r2, 1
SRAI32 R1, R2, 1

# CHECK: srai32 r3, r4, 31
SRAI32 R3, R4, 31

#===----------------------------------------------------------------------===
# Move and Load Immediate
#===----------------------------------------------------------------------===

# CHECK: move32 r5, r6
MOVE32 R5, R6

# CHECK: lui r7, 42
LUI R7, 42

# CHECK: lui r8, 4095
LUI R8, 0xFFF

#===----------------------------------------------------------------------===
# DSP/Miscellaneous Operations
#===----------------------------------------------------------------------===

# CHECK: abs32s r9, r10
ABS32S R9, R10

# CHECK: max32 r11, r12, r0
MAX32 R11, R12, R0

# CHECK: maxu32 r1, r2, r3
MAXU32 R1, R2, R3

# CHECK: min32 r4, r5, r6
MIN32 R4, R5, R6

# CHECK: minu32 r7, r8, r9
MINU32 R7, R8, R9

# CHECK: neg32 r10, r11
NEG32 R10, R11

# CHECK: neg32s r12, r0
NEG32S R12, R0

# CHECK: brev32 r1, r2, r3
BREV32 R1, R2, R3

# CHECK: nsa32 r4, r5
NSA32 R4, R5

# CHECK: nsau32 r6, r7
NSAU32 R6, R7

# CHECK: popcount32 r8, r9
POPCOUNT32 R8, R9

#===----------------------------------------------------------------------===
# Compare/Conditional Operations
#===----------------------------------------------------------------------===

# CHECK: slt32 r10, r11, r12
SLT32 R10, R11, R12

# CHECK: sltu32 r0, r1, r2
SLTU32 R0, R1, R2

# CHECK: sle32 r3, r4, r5
SLE32 R3, R4, R5

# CHECK: seq32 r6, r7, r8
SEQ32 R6, R7, R8

# NOTE: SNE32 not defined — use SEQ32 + invert.

# CHECK: movt32 r12, r0, r1
MOVT32 R12, R0, R1

# CHECK: movf32 r2, r3, r4
MOVF32 R2, R3, R4

#===----------------------------------------------------------------------===
# Multiplication
#===----------------------------------------------------------------------===
# Scalar MUL32 removed : not in the ISA DB (the slot-0 scalar ALU has no
# multiply unit). Scalar s32 multiply now lowers to sext32t64 x2 + mul64.ll +
# move32_dr_l on the slot-1/2 DR64 unit, so there is no scalar GPR "mul" to
# round-trip here. SIMD X2MUL32 and the MUL64_* family are covered in their
# own MC files.
