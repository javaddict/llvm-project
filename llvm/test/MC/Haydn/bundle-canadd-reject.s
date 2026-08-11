# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target
#
# B3.7 negative lit: AsmParser Bundle.canAdd fail-closed
# (AIEBaseAsmParser.h:192-201 processMatchedInstruction → Error "incorrect
# bundle"). Illegal co-issue must not emit a parcel.
#
# Every rejection below is PLACEMENT exhaustion: the instructions in the
# bundle have no assignment of (entry position) left that a composite covers.
# That is the only half of format E's rule the assembler currently enforces —
# the unit half does not fire on this path, and bundle-unit-collision.s is
# the XFAIL that records it. Do not read a rejection here as evidence that
# units are checked.
#
# The case this test USED to open with was three ADD64: under Bundle128 only
# S1|S2 could take one, so the third saturated and failed canAdd. Format E
# has three ALU units and slots carry no capability, so that bundle is now
# LEGAL and its rejection was the premise going stale rather than an
# assertion. It is kept below as the positive contrast, because a negative
# test whose illegal cases quietly become legal is worse than no test.

# Four ADD32: there is no four-entry composite, so the fourth has nowhere to
# go regardless of unit. This is the count bound that survives.
# CHECK: error: incorrect bundle
{ add32 r0, r1, r2; add32 r3, r4, r5; add32 r6, r7, r8; add32 r9, r10, r11 }

# BEQ / BNE / BLT each exist only at 3-entry positions 0 and 1 (P30, P31).
# Three of them need a third position and none of them has one.
# CHECK: error: incorrect bundle
{ beq r1, r2, 8; bne r3, r4, 16; blt r5, r6, 24 }

# D_STWUA_POST and D_SQHWUA_POST exist only at position 0 (P20, P30), so two
# of them collide on the position before the unit axis is even consulted.
# CHECK: error: incorrect bundle
{ d_stwua_post d0, 0, r1; d_sqhwua_post d1, 0, r2 }

# Positive contrast, and the reason the old first case had to go: three ADD64
# on ALU0/ALU1/ALU2 is exactly what format E made possible. llvm-mc still
# exits non-zero for the bundles above, so this line reaches FileCheck on the
# same stream, after the errors.
# CHECK: { {{.*}}add64{{.*}}d0, d1, d2; {{.*}}add64{{.*}}d3, d4, d5; {{.*}}add64{{.*}}d6, d7, d8 }
{ add64 d0, d1, d2; add64 d3, d4, d5; add64 d6, d7, d8 }
