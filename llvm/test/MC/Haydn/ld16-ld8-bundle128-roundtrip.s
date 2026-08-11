# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck --check-prefix=ASM %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o \
# RUN:   | FileCheck --check-prefix=OBJ %s --implicit-check-not='<unknown>'
#
# Halfword / byte load encode + disasm, and the SCALE each one carries.
#
# The immediate is an ELEMENT index, not a byte offset — the database states
# it per instruction and the widths differ:
#
#   S_LBS_WITH_IMM   rt = SEXT8->32(mem8[rs + imm6])          scale 1
#   S_LHWS_WITH_IMM  rt = SEXT16->32(mem16[rs + (imm6 << 1)]) scale 2
#   S_LW_WITH_IMM    rt = mem32[rs + (imm6 << 2)]             scale 4
#   D_LDW_WITH_IMM   rtd = mem64[rs + (imm6 << 3)]            scale 8
#
# These checks used to spell the BYTE offset while the source spelled the
# element index, so `s_lhws_with_imm r5, r6, 1` was asserted to print back as
# `r5, r6, 2`.
#
# WHAT THIS FILE CANNOT CHECK, so that nobody reads more into it: the SCALE
# itself. The encoding holds imm6 and nothing else — the shift is what the
# hardware does with it, so an encoder and decoder that agreed on a wrong
# shift would still round-trip here, and no MC test can see the difference.
# The scale is checked where a byte offset has to be converted into an
# element index, which is CodeGen: llvm/test/CodeGen/Haydn/s64-loadstore.ll
# spills the halves of an i64 at 0 and 1, not 0 and 4. Past that the
# simulator is the only judge.
#
# What the ladder at the bottom does do is pin that the four widths take
# DIFFERENT immediates for one byte offset. That catches the regression these
# very edits could introduce — a parser or printer going back to byte
# spelling — and it makes the four scales readable in one place.

# ASM: { s_lhws_{{[a-z_]*}}{{.*}} r3, r4, 0
# ASM: { s_lhws_{{[a-z_]*}}{{.*}} r5, r6, 1
# ASM: { s_lbs_{{[a-z_]*}}{{.*}} r7, r8, 0
# ASM: { s_lbs_{{[a-z_]*}}{{.*}} r9, r10, 1
s_lhws_with_imm r3, r4, 0
s_lhws_with_imm r5, r6, 1
s_lbs_with_imm  r7, r8, 0
s_lbs_with_imm  r9, r10, 1

# OBJ: {{.*}} s_lhws_{{[a-z_]*}}{{.*}} r3, r4, 0
# OBJ: {{.*}} s_lhws_{{[a-z_]*}}{{.*}} r5, r6, 1
# OBJ: {{.*}} s_lbs_{{[a-z_]*}}{{.*}} r7, r8, 0
# OBJ: {{.*}} s_lbs_{{[a-z_]*}}{{.*}} r9, r10, 1

#===----------------------------------------------------------------------===#
# The scale ladder: FOUR widths reaching the SAME byte offset, +8 from r1.
#
# byte 8 / 1 = 8 for the byte load, / 2 = 4 halfword, / 4 = 2 word,
# / 8 = 1 doubleword. The four immediates are forced to 8, 4, 2, 1 by the
# database, and writing them together is what makes an operand that quietly
# went back to meaning bytes impossible to miss: it would need all four to
# read 8.
#===----------------------------------------------------------------------===#

# OBJ: {{.*}} s_lbs_{{[a-z_]*}}{{.*}} r2, r1, 8
# OBJ: {{.*}} s_lhws_{{[a-z_]*}}{{.*}} r3, r1, 4
# OBJ: {{.*}} s_lw_{{[a-z_]*}}{{.*}} r4, r1, 2
# OBJ: {{.*}} d_ldw_{{[a-z_]*}}{{.*}} d0, r1, 1
s_lbs_with_imm  r2, r1, 8
s_lhws_with_imm r3, r1, 4
s_lw_with_imm   r4, r1, 2
d_ldw_with_imm  d0, r1, 1
