# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Out-of-range BEQ → R0 soft-zero borrow veneer (3 × production EncodedBytes).

# RELOCS: R_HAYDN_WIDE_BranchSImm12_RI far_target

# NM-DAG: __haydn_thunk_far_target
# NM-DAG: _start
# NM-DAG: far_target

.section .text
.globl _start
_start:
    BEQ R0, R1, far_target
    ADD32 R2, R2, R2
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    ADD32 R3, R3, R3
    .size far_target, .-far_target

# CHECK-LABEL: <_start>:
# CHECK: {{.*}} beq{{.*}}r0,{{.*}}r1,
# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK-NOT: r12
# CHECK-NOT: subi32
# CHECK-LABEL: <far_target>:
# CHECK: {{.*}} add32
