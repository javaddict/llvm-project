# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

#===----------------------------------------------------------------------===
# Test branch target relocations
#===----------------------------------------------------------------------===

# Forward branch reference
# CHECK: beq r0, r1, forward_label
BEQ R0, R1, forward_label

forward_label:
ADD32 R2, R3, R4

# Backward branch reference
# CHECK: bne r5, r6, backward_label
backward_label:
BNE R5, R6, backward_label

#===----------------------------------------------------------------------===
# Test unconditional branch relocations
#===----------------------------------------------------------------------===

# Forward unconditional branch
# CHECK: beqz r7, forward_target
BEQZ R7, forward_target

# Backward unconditional branch
# CHECK: bnez r8, backward_target
backward_target:
BNEZ R8, backward_target

forward_target:
ADD32 R9, R10, R11

#===----------------------------------------------------------------------===
# Test jump and link relocations
#===----------------------------------------------------------------------===

# JAL to external symbol
# CHECK: jal r0, external_func
JAL R0, external_func

#===----------------------------------------------------------------------===
# Test load/store with symbolic offsets
#===----------------------------------------------------------------------===

# Load with symbol reference (requires relocation)
# CHECK: s_lw_with_imm r3, r4, symbol_offset
S_LW_WITH_IMM R3, R4, symbol_offset

# Store with symbol reference
# CHECK: s_sw_with_imm r5, r6, data_location
S_SW_WITH_IMM R5, R6, data_location

#===----------------------------------------------------------------------===
# Test PC-relative references
#===----------------------------------------------------------------------===

# PC-relative branch
# CHECK: blt r10, r11, local_loop
local_func:
local_loop:
BLT R10, R11, local_loop

# Test far branch
# CHECK: bge r12, r0, far_target
BGE R12, R0, far_target

# Skip some instructions to create distance
ADD32 R1, R1, R1
ADD32 R2, R2, R2
ADD32 R3, R3, R3
ADD32 R4, R4, R4
ADD32 R5, R5, R5
ADD32 R6, R6, R6
ADD32 R7, R7, R7
ADD32 R8, R8, R8
ADD32 R9, R9, R9
ADD32 R10, R10, R10

far_target:
ADD32 R11, R11, R11
