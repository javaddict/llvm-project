# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf %s --defsym=OOB=1 -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=OOB

# Role: object — ANDI/ORI/XORI32 are ZEXT imm20 (uimm20). Golden `imm20`
# is a width pin; catalog semantics are unsigned. Bit 19 must print as
# 524288, not sign-decode to -524288 (ADDI32 is the signed sibling).

.ifndef OOB
.text
  { andi32 r1, r2, 524288 }
  { ori32 r3, r4, 524288 }
  { xori32 r5, r6, 1048575 }
  { andi32 r7, r8, 0 }
  { sin_cos d0, r1, 15 }
  { d_ldw_cb_imm 0, d0, r1, -128 }

# CHECK-LABEL: <.text>:
# CHECK: andi32{{.*}}r1, r2, 524288
# CHECK: ori32{{.*}}r3, r4, 524288
# CHECK: xori32{{.*}}r5, r6, 1048575
# CHECK: andi32{{.*}}r7, r8, 0
# CHECK: sin_cos{{.*}}d0, r1, 15
# CHECK: d_ldw_cb_imm{{.*}}0, d0, r1, -128
# CHECK-NOT: -524288
# CHECK-NOT: -1
.endif

.ifdef OOB
# OOB: error: unknown error matching instruction
andi32 r1, r2, 1048576
sin_cos d0, r1, 16
d_ldw_cb_imm 0, d0, r1, -129
.endif
