# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
#
# Load/store round trip. The first RUN is asm → parse → print; the second is
# the one that was "deferred until the disassembler is complete" and is not
# deferred any more — asm → encode → decode → print, which is what makes this
# a round trip rather than a spelling check.
#
# Spellings and immediates are format E's: the § 5.6 rename retired
# ld32/st32/ld8/…, and the immediate is an ELEMENT INDEX in a simm6 field, so
# the byte offsets this file used to carry (64, -128) are out of range as
# written and mean something else besides.

# Basic load/store (32-bit)
# CHECK: s_lw_with_imm r0, r1, 0
s_lw_with_imm r0, r1, 0

# CHECK: s_lw_with_imm r2, r3, 4
s_lw_with_imm r2, r3, 4

# CHECK: s_sw_with_imm r4, r5, 0
s_sw_with_imm r4, r5, 0

# CHECK: s_sw_with_imm r6, r7, -1
s_sw_with_imm r6, r7, -1

# Load/store size variants
# CHECK: s_lhws_with_imm r8, r9, 0
s_lhws_with_imm r8, r9, 0

# CHECK: s_lbs_with_imm r10, r11, 0
s_lbs_with_imm r10, r11, 0

# CHECK: s_lhwu_with_imm r12, r0, 0
s_lhwu_with_imm r12, r0, 0

# CHECK: s_lbu_with_imm r1, r2, 0
s_lbu_with_imm r1, r2, 0

# CHECK: s_shw_with_imm r3, r4, 0
s_shw_with_imm r3, r4, 0

# CHECK: s_sb_with_imm r5, r6, 0
s_sb_with_imm r5, r6, 0

# 64-bit load
# CHECK: d_ldw_with_imm d0, r1, 0
d_ldw_with_imm d0, r1, 0

# Both ends of the field, which is where a sign or width slip shows
# CHECK: s_lw_with_imm r7, r8, 16
s_lw_with_imm r7, r8, 16

# CHECK: s_lw_with_imm r9, r10, -16
s_lw_with_imm r9, r10, -16

# CHECK: s_sw_with_imm r11, r12, 31
s_sw_with_imm r11, r12, 31

# CHECK: s_sw_with_imm r0, r1, -32
s_sw_with_imm r0, r1, -32
