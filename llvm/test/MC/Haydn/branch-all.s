# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Comprehensive branch instruction test.
# Only includes instructions actually defined in HaydnInstrInfo.td.
# NOTE: BLE, BLEU, BGT, BGTU, BLEZ, BGTZ, J, JR, CALLNEQZ, CALLEQZ,
# BL, BLEQZ, BEQ.L, BNE.L are NOT defined — removed from test.
# Haydn uses BEQ/BNE/BGE/BGEU/BLT/BLTU for comparisons and BGE+swap
# for BLE/BGT semantics.

#===----------------------------------------------------------------------===
# Conditional Branches (Two-Operand)
#===----------------------------------------------------------------------===#

target1:
# CHECK: beq r0, r1, target1
BEQ R0, R1, target1

# CHECK: beq r2, r3, target2
BEQ R2, R3, target2

target2:
# CHECK: bne r4, r5, target2
BNE R4, R5, target2

# CHECK: bne r6, r7, target1
BNE R6, R7, target1

target3:
# CHECK: bge r8, r9, target3
BGE R8, R9, target3

# CHECK: bge r10, r11, target1
BGE R10, R11, target1

target4:
# CHECK: bgeu r12, r0, target4
BGEU R12, R0, target4

# CHECK: bgeu r1, r2, target1
BGEU R1, R2, target1

target5:
# CHECK: blt r3, r4, target5
BLT R3, R4, target5

# CHECK: blt r5, r6, target1
BLT R5, R6, target1

target6:
# CHECK: bltu r7, r8, target6
BLTU R7, R8, target6

# CHECK: bltu r9, r10, target1
BLTU R9, R10, target1

#===----------------------------------------------------------------------===
# Conditional Branches (One-Operand with Zero)
#===----------------------------------------------------------------------===#

target11:
# CHECK: beqz r1, target11
BEQZ R1, target11

# CHECK: beqz r2, target1
BEQZ R2, target1

target12:
# CHECK: bnez r3, target12
BNEZ R3, target12

# CHECK: bnez r4, target1
BNEZ R4, target1

target13:
# CHECK: bgez r5, target13
BGEZ R5, target13

# CHECK: bgez r6, target1
BGEZ R6, target1

target14:
# CHECK: bltz r7, target14
BLTZ R7, target14

# CHECK: bltz r8, target1
BLTZ R8, target1

#===----------------------------------------------------------------------===
# Jump and Link (Call)
#===----------------------------------------------------------------------===#

target18:
# CHECK: jal r2, target18
JAL R2, target18

# CHECK: jal r3, target1
JAL R3, target1

# CHECK: jalr r4, r5, target19
JALR R4, R5, target19

target19:
# CHECK: jalr r6, r7, target1
JALR R6, R7, target1

# NOTE: RET is a pseudo-instruction (HaydnPseudo), not a real MC instruction.
# It expands to "jalr r0, lr, 0" during codegen. Cannot test in llvm-mc.
