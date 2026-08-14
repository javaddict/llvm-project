# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
#
# Every scalar load/store mnemonic, and the range of the field they share.
#
# The spellings are the format E ones. `ld32`/`st32`/`ld8`/`ldu16`/… were
# retired by the § 5.6 rename and are not the same instructions under new
# names: the immediate stopped being a BYTE offset and became an ELEMENT
# INDEX, with the hardware computing EA = rs + (imm << log2(width)). The old
# values here reflected that — `ld32 r6, r7, 1024` — and 1024 is 256 elements,
# which no longer fits.
#
# So the field is `simm6`: -32 to 31, for every width. That is what the values
# below walk, and the last of each group sits on an end of it.
#
# What this test canNOT see, and no MC test can: the SCALE. The encoding holds
# the six-bit field and nothing else, so `s_lw_with_imm r1, r2, 1` and
# `s_lbs_with_imm r1, r2, 1` differ in opcode and not in immediate, and a
# swapped shift amount between two widths round-trips clean. The scale is
# checked where it is observable — by executing, in the simulator's
# cb99_negative_ls.

#===----------------------------------------------------------------------===
# Scalar loads
#===----------------------------------------------------------------------===

# CHECK: s_lw_with_imm r0, r1, 0
s_lw_with_imm r0, r1, 0

# CHECK: s_lw_with_imm r2, r3, 1
s_lw_with_imm r2, r3, 1

# CHECK: s_lw_with_imm r4, r5, -2
s_lw_with_imm r4, r5, -2

# CHECK: s_lw_with_imm r6, r7, 31
s_lw_with_imm r6, r7, 31

# CHECK: s_lhws_with_imm r8, r9, 0
s_lhws_with_imm r8, r9, 0

# CHECK: s_lhws_with_imm r10, r11, 1
s_lhws_with_imm r10, r11, 1

# CHECK: s_lhws_with_imm r12, r0, -2
s_lhws_with_imm r12, r0, -2

# CHECK: s_lbs_with_imm r1, r2, 0
s_lbs_with_imm r1, r2, 0

# CHECK: s_lbs_with_imm r3, r4, 1
s_lbs_with_imm r3, r4, 1

# CHECK: s_lbs_with_imm r5, r6, -2
s_lbs_with_imm r5, r6, -2

# CHECK: s_lhwu_with_imm r7, r8, 0
s_lhwu_with_imm r7, r8, 0

# CHECK: s_lhwu_with_imm r9, r10, 1
s_lhwu_with_imm r9, r10, 1

# CHECK: s_lhwu_with_imm r11, r12, 31
s_lhwu_with_imm r11, r12, 31

# CHECK: s_lbu_with_imm r0, r1, 0
s_lbu_with_imm r0, r1, 0

# CHECK: s_lbu_with_imm r2, r3, 1
s_lbu_with_imm r2, r3, 1

# CHECK: s_lbu_with_imm r4, r5, 16
s_lbu_with_imm r4, r5, 16

#===----------------------------------------------------------------------===
# Scalar stores
#===----------------------------------------------------------------------===

# CHECK: s_sw_with_imm r6, r7, 0
s_sw_with_imm r6, r7, 0

# CHECK: s_sw_with_imm r8, r9, 1
s_sw_with_imm r8, r9, 1

# CHECK: s_sw_with_imm r10, r11, -2
s_sw_with_imm r10, r11, -2

# CHECK: s_sw_with_imm r12, r0, -32
s_sw_with_imm r12, r0, -32

# CHECK: s_shw_with_imm r1, r2, 0
s_shw_with_imm r1, r2, 0

# CHECK: s_shw_with_imm r3, r4, 1
s_shw_with_imm r3, r4, 1

# CHECK: s_shw_with_imm r5, r6, -2
s_shw_with_imm r5, r6, -2

# CHECK: s_shw_with_imm r7, r8, 31
s_shw_with_imm r7, r8, 31

# CHECK: s_sb_with_imm r9, r10, 0
s_sb_with_imm r9, r10, 0

# CHECK: s_sb_with_imm r11, r12, 1
s_sb_with_imm r11, r12, 1

# CHECK: s_sb_with_imm r0, r1, -2
s_sb_with_imm r0, r1, -2

# CHECK: s_sb_with_imm r2, r3, 31
s_sb_with_imm r2, r3, 31

#===----------------------------------------------------------------------===
# DR64 load/store
#===----------------------------------------------------------------------===

# CHECK: d_ldw_with_imm d0, r7, 0
d_ldw_with_imm d0, r7, 0

# CHECK: d_ldw_with_imm d1, r8, 1
d_ldw_with_imm d1, r8, 1

# CHECK: d_sdw_with_imm d2, r9, 0
d_sdw_with_imm d2, r9, 0

# CHECK: d_sdw_with_imm d3, r10, -2
d_sdw_with_imm d3, r10, -2
