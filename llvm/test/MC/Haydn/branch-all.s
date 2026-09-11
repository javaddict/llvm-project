# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Comprehensive branch instruction test.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK: {{.*}}0: 07 0d 04 01 00 00 00 00 00 00 00 00{{.*}}beq
# CHECK: {{.*}}c: 07 0d 24 03 0c 00 00 00 00 00 00 00{{.*}}beq
# CHECK: {{.*}}18: 07 0d 46 05 00 00 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}24: 07 0d 66 07 dc 0f 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}30: 07 0d 88 09 00 00 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}3c: 07 0d a8 0b c4 0f 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}48: 07 0d cc 00 00 00 00 00 00 00 00 00{{.*}}bgeu
# CHECK: {{.*}}54: 07 0d 1c 02 ac 0f 00 00 00 00 00 00{{.*}}bgeu
# CHECK: {{.*}}60: 07 0d 3a 04 00 00 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}6c: 07 0d 5a 06 94 0f 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}78: 07 0d 7e 08 00 00 00 00 00 00 00 00{{.*}}bltu
# CHECK: {{.*}}84: 07 0d 9e 0a 7c 0f 00 00 00 00 00 00{{.*}}bltu
# CHECK: {{.*}}90: 07 0a 18 00 00 00 00 00 00 00 00 00{{.*}}beqz
# CHECK: {{.*}}9c: 07 0a 28 00 64 0f 00 00 00 00 00 00{{.*}}beqz
# CHECK: {{.*}}a8: 07 0a 3a 00 00 00 00 00 00 00 00 00{{.*}}bnez
# CHECK: {{.*}}b4: 07 0a 4a 00 4c 0f 00 00 00 00 00 00{{.*}}bnez
# CHECK: {{.*}}c0: 07 0a 5c 00 00 00 00 00 00 00 00 00{{.*}}bgez
# CHECK: {{.*}}cc: 07 0a 6c 00 34 0f 00 00 00 00 00 00{{.*}}bgez
# CHECK: {{.*}}d8: 07 0a 7e 00 00 00 00 00 00 00 00 00{{.*}}bltz
# CHECK: {{.*}}e4: 07 0a 8e 00 1c 0f 00 00 00 00 00 00{{.*}}bltz
# CHECK: {{.*}}f0: 07 0e 28 00 00 00 00 00 00 00 00 00{{.*}}jal
# CHECK: {{.*}}fc: 07 0e 38 00 82 ff 07 00 00 00 00 00{{.*}}jal
# CHECK: {{.*}}108: 07 0d 42 05 0c 00 00 00 00 00 00 00{{.*}}jalr
# CHECK: {{.*}}114: 07 0d 62 07 00 00 00 00 00 00 00 00{{.*}}jalr
# CHECK-NOT: <unknown>

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
BEQ R0, R1, target1

BEQ R2, R3, target2

target2:
BNE R4, R5, target2

BNE R6, R7, target1

target3:
BGE R8, R9, target3

BGE R10, R11, target1

target4:
BGEU R12, R0, target4

BGEU R1, R2, target1

target5:
BLT R3, R4, target5

BLT R5, R6, target1

target6:
BLTU R7, R8, target6

BLTU R9, R10, target1

#===----------------------------------------------------------------------===
# Conditional Branches (One-Operand with Zero)
#===----------------------------------------------------------------------===#

target11:
BEQZ R1, target11

BEQZ R2, target1

target12:
BNEZ R3, target12

BNEZ R4, target1

target13:
BGEZ R5, target13

BGEZ R6, target1

target14:
BLTZ R7, target14

BLTZ R8, target1

#===----------------------------------------------------------------------===
# Jump and Link (Call)
#===----------------------------------------------------------------------===#

target18:
JAL R2, target18

JAL R3, target1

# ISA-69: symbolic JALR is fail-closed. Literals keep the object
# roundtrip; imm12=12 is one parcel, imm12=0 is register-indirect.
JALR R4, R5, 12

target19:
JALR R6, R7, 0

# NOTE: RET is a pseudo-instruction (HaydnPseudo), not a real MC instruction.
# It expands to "jalr r0, lr, 0" during codegen. Cannot test in llvm-mc.
