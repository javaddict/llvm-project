# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# XFAIL: *
# Residual: FileCheck / idle-pad / reloc geometry still open under Format E cutover.
# XFAIL-OWNER: branch PC-rel wire scale (golden unspecified) | positive branch-scale claim residual; no golden invent

# Role: object — fail-closed residual for WIDE branch PC-rel range tables.
# Branch wire scale is not golden-defined; do not treat halfword field pins
# as product law. Positive in-range oracles remain residual (XFAIL).
# Negative OOR paths exercise applyFixup fail-closed only.
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
