# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION (F03/D177): long-branch thunk opcodes must be valid Bundle128
# LUI+ADDI32+JALR through R12 — not multi-width placeholders.

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

# Address order: thunk, _start, far_target.
# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK: {{.*}} lui{{.*}}r12,
# CHECK: {{.*}} addi32{{.*}}r12,{{.*}}r12,
# CHECK: {{.*}} jalr{{.*}}r0,{{.*}}r12
# CHECK-NOT: lui_w
# CHECK-NOT: jalr_w
# CHECK-LABEL: <_start>:
# CHECK: {{.*}} beq
# CHECK-LABEL: <far_target>:
# CHECK: {{.*}} add32
