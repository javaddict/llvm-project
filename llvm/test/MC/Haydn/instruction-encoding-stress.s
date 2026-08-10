# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
#
# Bundle128 MC encode→disasm stress for core LS / ALU32 / ALU64 ops.
# Absolute PCs are not pinned (16-byte packets). CHECK matches mnemonics
# and operands in order. Phantom mul32 / broken WideImm slli64 forms removed.
# Dedicated LD16/LD8 gate: ld16-ld8-bundle128-roundtrip.s
# HI12/LO20 gate: cb76-reloc-hi20-lo16.s
#
# Note: objdump may omit spaces between mnemonic and first operand
# (ld16r3 …). Patterns use {{.*}} flex.

#===----------------------------------------------------------------------===
# Load / store
#===----------------------------------------------------------------------===

# CHECK: {{.*}} ld32{{.*}} r0, r1, 0
s_lw_with_imm r0, r1, 0
# CHECK: {{.*}} ld32{{.*}} r2, r3, 4
s_lw_with_imm r2, r3, 1
# CHECK: {{.*}} ld32{{.*}} r4, r5, -4
s_lw_with_imm r4, r5, -1

# CHECK: {{.*}} ld16{{.*}} r3, r4, 0
s_lhws_with_imm r3, r4, 0
# CHECK: {{.*}} ld16{{.*}} r5, r6, 2
s_lhws_with_imm r5, r6, 1
# CHECK: {{.*}} ldu16{{.*}} r11, r12, 0
s_lhwu_with_imm r11, r12, 0
# CHECK: {{.*}} st16{{.*}} r2, r3, 0
s_shw_with_imm r2, r3, 0

# CHECK: {{.*}} ld8{{.*}} r10, r11, 0
s_lbs_with_imm r10, r11, 0
# CHECK: {{.*}} ldu8{{.*}} r5, r6, 0
s_lbu_with_imm r5, r6, 0
# CHECK: {{.*}} st8{{.*}} r9, r10, 0
s_sb_with_imm r9, r10, 0

# CHECK: {{.*}} ld64{{.*}} d0, r4, 0
d_ldw_with_imm d0, r4, 0
# CHECK: {{.*}} st64{{.*}} d0, r8, 0
d_sdw_with_imm d0, r8, 0

#===----------------------------------------------------------------------===
# ALU32
#===----------------------------------------------------------------------===

# CHECK: {{.*}} add32{{.*}} r0, r1, r2
add32 r0, r1, r2
# CHECK: {{.*}} addi32{{.*}} r3, r4, 0
addi32 r3, r4, 0
# CHECK: {{.*}} addi32{{.*}} r5, r6, 32767
addi32 r5, r6, 32767
# 1 in RI20 prints as signed decimal
# CHECK: {{.*}} addi32{{.*}} r7, r8, -1
addi32 r7, r8, -1
# CHECK: {{.*}} andi32{{.*}} r9, r10, 65535
andi32 r9, r10, 65535
# CHECK: {{.*}} slli32{{.*}} r0, r1, 0
slli32 r0, r1, 0
# CHECK: {{.*}} slli32{{.*}} r2, r3, 31
slli32 r2, r3, 31
# LUI is HI12 (0..4095), not legacy HI20
# CHECK: {{.*}} lui{{.*}} r4, 0
lui r4, 0
# CHECK: {{.*}} lui{{.*}} r5, 4095
lui r5, 4095
# CHECK: {{.*}} not32{{.*}} r3, r4
not32 r3, r4
# CHECK: {{.*}} move32{{.*}} r5, r6
move32 r5, r6
# CHECK: {{.*}} slt32{{.*}} r10, r11, r12
slt32 r10, r11, r12
# CHECK: {{.*}} neg32{{.*}} r1, r2
neg32 r1, r2
# CHECK: {{.*}} nsa32{{.*}} r12, r0
nsa32 r12, r0

#===----------------------------------------------------------------------===
# ALU64 / SIMD
#===----------------------------------------------------------------------===

# CHECK: {{.*}} add64{{.*}} d0, d1, d2
add64 d0, d1, d2
# CHECK: {{.*}} add64{{.*}} d15, d0, d1
add64 d15, d0, d1
# CHECK: {{.*}} xor64{{.*}} d0, d1, d2
xor64 d0, d1, d2
# CHECK: {{.*}} x2add32{{.*}} d9, d10, d11
x2add32 d9, d10, d11
# CHECK: {{.*}} x4add16{{.*}} d2, d3, d4
x4add16 d2, d3, d4
# CHECK: {{.*}} x2mul32{{.*}} d15, d0, d1, d2
x2mul32 d15, d0, d1, d2

#===----------------------------------------------------------------------===
# CSR
#===----------------------------------------------------------------------===

# CHECK: {{.*}} csrr{{.*}} r2, 0
csrr r2, 0
# CHECK: {{.*}} csrw{{.*}} 0, r4
csrw 0, r4
