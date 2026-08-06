# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s

# Role: object — Round-trip test for branch instructions: asm → parse → print.

# Round-trip test for branch instructions: asm → parse → print.
# NOTE: Full encode→decode roundtrip deferred until disassembler is complete.

# Conditional branches (two-register)

branch_target:
# CHECK: beq r4, r5, branch_target
beq r4, r5, branch_target

# CHECK: bne r6, r7, branch_target
bne r6, r7, branch_target

# CHECK: bge r8, r9, branch_target
bge r8, r9, branch_target

# CHECK: bgeu r10, r11, branch_target
bgeu r10, r11, branch_target

# CHECK: blt r12, r0, branch_target
blt r12, r0, branch_target

# CHECK: bltu r1, r2, branch_target
bltu r1, r2, branch_target

# Conditional branches (one-register)
# CHECK: beqz r3, branch_target
beqz r3, branch_target

# CHECK: bnez r4, branch_target
bnez r4, branch_target

# CHECK: bgez r5, branch_target
bgez r5, branch_target

# CHECK: bltz r6, branch_target
bltz r6, branch_target

# Jump and link instructions
# CHECK: jal r7, external_call
jal r7, external_call

# CHECK: jalr r8, r9, external_call
jalr r8, r9, external_call

external_call:
