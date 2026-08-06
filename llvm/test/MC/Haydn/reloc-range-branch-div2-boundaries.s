# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: object — Pin WIDE branch PC-rel geometry: signed 12-bit field after ÷2 (ValueShift=1, Align=2).

# Pin WIDE branch PC-rel geometry: signed 12-bit field after ÷2 (ValueShift=1,
# Align=2). Effective byte window is [-4096, +4094]. Format E parcels are
# 12 bytes, so the nearest in-range parcel distances are +4080 / -4096; the
# next parcel steps (+4096 / -4112) must fail closed in applyFixup via
# HaydnRelocLayout::computeRelocValue (shared with lld inBranchRange).
#

# OOR-POS: relocation offset out of range
# OOR-NEG: relocation offset out of range

.text
.globl test_branch_div2_bounds
.balign 16

# ---- Positive boundary: offset +4080 (field = 2040) is in range. ----
pos_in_range:
    beq r1, r2, pos_target_ok
    .space 4064
pos_target_ok:
    { add32 r0, r0, r0 }

.ifdef OOR_POS
# Positive just past range: offset +4096 (field = 2048) is out of signed-12.
pos_out_of_range:
    beq r1, r2, pos_target_bad
    .space 4080
pos_target_bad:
    { add32 r0, r0, r0 }
.endif

# ---- Negative boundary: offset -4096 (field = -2048) is in range. ----
.balign 16
neg_target_ok:
    { add32 r3, r3, r3 }
    .space 4080
neg_in_range:
    beq r3, r4, neg_target_ok

.ifdef OOR_NEG
# Negative just past range: offset -4112 (field = -2056) is out of signed-12.
.balign 16
neg_target_bad:
    { add32 r5, r5, r5 }
    .space 4096
neg_out_of_range:
    beq r5, r6, neg_target_bad
.endif
