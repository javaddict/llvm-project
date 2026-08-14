# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
#
# REGRESSION TEST: GE96-03 WIDE branch PC-rel is byte PC+imm (ValueShift=0).
# Signed 12-bit field, Align=2 → even byte window [-2048, +2046].
# Parcel-aligned code can only use multiples of 12, so the in-range
# probes are +2040 / -2040. If ÷2 returns, +2040 still encodes and
# +2048 is no longer OOR.
#
# Each Format E parcel is 12 bytes. beq + .space N → offset 12+N.
# .space sizes are multiples of 12 so parcels stay 12-aligned
# (align-16 after a non-multiple requested a 4-byte nop pad and aborted).

# CHECK: beq{{.*}}2040
# CHECK: beq{{.*}}-2040
# OOR-POS: relocation offset out of range
# OOR-NEG: relocation offset out of range

.text
.globl test_branch_byte_bounds

# ---- Positive in range: offset +2040. ----
pos_in_range:
    beq r1, r2, pos_target_ok
    .space 2028
pos_target_ok:
    { add32 r0, r0, r0 }

.ifdef OOR_POS
# Positive just past range: offset +2048 is out of signed-12.
pos_out_of_range:
    beq r1, r2, pos_target_bad
    .space 2036
pos_target_bad:
    { add32 r0, r0, r0 }
.endif

# ---- Negative in range: offset -2040 (largest 12-multiple in window). ----
neg_target_ok:
    { add32 r3, r3, r3 }
    .space 2028
neg_in_range:
    beq r3, r4, neg_target_ok

.ifdef OOR_NEG
# Negative just past range: offset -2052 (next 12-multiple below -2048).
neg_target_bad:
    { add32 r5, r5, r5 }
    .space 2040
neg_out_of_range:
    beq r5, r6, neg_target_bad
.endif
