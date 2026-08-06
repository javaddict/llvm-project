# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION TEST (L228): lld must DISPATCH WIDE branch/call relocations
# through getRelExpr / getImplicitAddend / inBranchRange / needsThunk.
#
# Bundle128 (D456/D487): every parcel is 16 bytes. WIDE jal / beq still
# emit R_HAYDN_WIDE_CallSImm20 / R_HAYDN_WIDE_BranchSImm12; reloc offsets are
# 0x0 and 0x10 (not legacy 6-byte parcel spacing 0x0/0x6).

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_WIDE_CallSImm20 callee 0x0
# RELOCS-DAG:      0x10 R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} callee 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal
    jal lr, callee

    # CHECK: 10010: {{.*}} beq
    beq r1, r2, callee

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    { add32 r1, r2, r3 }
    .size callee, .-callee

    .size _start, .-_start
