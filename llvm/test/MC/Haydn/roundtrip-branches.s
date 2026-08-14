# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — Round-trip test for branch instructions: asm → parse → print.
# Converted from parse-only to product MC contract (encode→obj→disasm).
# CHECKs regenerated from live objdump (Format E 12-byte parcels).

# CHECK: {{.*}}0: 07 0d 44 05 00 00 00 00 00 00 00 00{{.*}}beq
# CHECK: {{.*}}c: 07 0d 66 07 f4 0f 00 00 00 00 00 00{{.*}}bne
# CHECK: {{.*}}18: 07 0d 88 09 e8 0f 00 00 00 00 00 00{{.*}}bge
# CHECK: {{.*}}24: 07 0d ac 0b dc 0f 00 00 00 00 00 00{{.*}}bgeu
# CHECK: {{.*}}30: 07 0d ca 00 d0 0f 00 00 00 00 00 00{{.*}}blt
# CHECK: {{.*}}3c: 07 0d 1e 02 c4 0f 00 00 00 00 00 00{{.*}}bltu
# CHECK: {{.*}}48: 07 0a 38 00 b8 0f 00 00 00 00 00 00{{.*}}beqz
# CHECK: {{.*}}54: 07 0a 4a 00 ac 0f 00 00 00 00 00 00{{.*}}bnez
# CHECK: {{.*}}60: 07 0a 5c 00 a0 0f 00 00 00 00 00 00{{.*}}bgez
# CHECK: {{.*}}6c: 07 0a 6e 00 94 0f 00 00 00 00 00 00{{.*}}bltz
# CHECK: {{.*}}78: 07 0e 78 00 0c 00 00 00 00 00 00 00{{.*}}jal
# CHECK: {{.*}}84: 07 0d 82 09 0c 00 00 00 00 00 00 00{{.*}}jalr
# CHECK-NOT: <unknown>

# Round-trip test for branch instructions: asm → parse → print.
# NOTE: Full encode→decode roundtrip deferred until disassembler is complete.

# Conditional branches (two-register)

branch_target:
beq r4, r5, branch_target

bne r6, r7, branch_target

bge r8, r9, branch_target

bgeu r10, r11, branch_target

blt r12, r0, branch_target

bltu r1, r2, branch_target

# Conditional branches (one-register)
beqz r3, branch_target

bnez r4, branch_target

bgez r5, branch_target

bltz r6, branch_target

# Jump and link instructions
jal r7, external_call

jalr r8, r9, external_call

external_call:
