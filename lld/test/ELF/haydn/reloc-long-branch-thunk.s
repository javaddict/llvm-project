# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: out-of-range BEQ must insert Bundle128 LUI+ADDI32+JALR thunk
# (R12 AT). Cross-section target so MC emits a reloc instead of failing
# applyFixup on a same-section .space gap.

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

# Address order: thunk @ low, then _start, then far.
# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK: {{.*}} lui{{.*}}r12,
# CHECK: {{.*}} addi32{{.*}}r12,
# CHECK: {{.*}} jalr{{.*}}r0,{{.*}}r12
# CHECK-LABEL: <_start>:
# CHECK: {{.*}} beq{{.*}}r0,{{.*}}r1,
# CHECK-LABEL: <far_target>:
# CHECK: {{.*}} add32
