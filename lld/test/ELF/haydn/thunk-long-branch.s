# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Far branch veneer: same R0 soft-zero borrow path as call (48 B).
# No R12 stack save — R12 is not linker AT.

.section .text
.globl _start
_start:
    BEQ R0, R1, far_target
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    ADD32 R2, R2, R2
    .size far_target, .-far_target

# CHECK-LABEL: <_start>:
# CHECK: {{.*}} beq
# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK: {{.*}} lui{{.*}}r0,
# CHECK: {{.*}} addi32{{.*}}r0,{{.*}}r0,
# CHECK: {{.*}} jalr{{.*}}r0,{{.*}}r0
# CHECK-NOT: r12
# CHECK-NOT: subi32
# CHECK-NOT: st32
# CHECK-NOT: lui_w
# CHECK-NOT: jalr_w
# CHECK-LABEL: <far_target>:
# CHECK: {{.*}} add32
