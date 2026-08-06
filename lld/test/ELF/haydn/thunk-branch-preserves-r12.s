# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t -T %S/haydn-far-branch.ld
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Far BEQ veneer borrows soft-zero R0 only — live R12 is never written by the
# veneer (no free AT / no R12 stack dance).

.section .text
.globl _start
_start:
    # Live R12 sentinel — veneer must not clobber it.
    addi32 r12, r0, 0x55
    beq r0, r1, far_target
    .size _start, .-_start

.section .text.far, "ax"
.globl far_target
far_target:
    add32 r2, r2, r2
    .size far_target, .-far_target

# NM: __haydn_thunk_far_target

# CHECK-LABEL: <_start>:
# CHECK: addi32{{.*}}r12
# CHECK: beq
# CHECK-LABEL: <__haydn_thunk_far_target>:
# CHECK-NOT: lui{{.*}}r12
# CHECK-NOT: st32{{.*}}r12
# CHECK-NOT: ld32{{.*}}r12
