# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION TEST (L228): lld must DISPATCH WIDE branch/call relocations
# through getRelExpr / getImplicitAddend / inBranchRange / needsThunk.
#
# Format E: every parcel is 12 bytes (EncodedBytes). WIDE jal_w / beq_w emit
# R_HAYDN_WIDE_CallSImm20 / R_HAYDN_WIDE_BranchSImm12; reloc offsets are
# 0x0 and 0xC. Printer may render jal_w/beq_w as jal/beq.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_WIDE_CallSImm20 callee 0x0
# RELOCS-DAG:      0xC R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} callee 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal{{(_w)?}}
    jal_w lr, callee

    # CHECK: 1000c: {{.*}} beq{{(_w)?}}
    beq_w r1, r2, callee

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    { add32 r1, r2, r3 }
    .size callee, .-callee

    .size _start, .-_start
