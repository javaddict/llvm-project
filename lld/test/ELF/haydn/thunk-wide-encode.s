# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Far call veneer (G-LLD-VENEER): LUI + ADDI32 + JALR on soft-zero R0.
# 3 × 16 = 48 bytes. No R12 (not a free AT). No post-JALR dead XOR.
#
#   0x10000: lui    r0, hi12(target)
#   0x1000c: addi32 r0, r0, lo20(target)
#   0x10018: jalr   r0, r0, 0

.section .text
.globl _start
_start:
    jal lr, callee
    .space 0x140000

.globl callee
callee:
    add32 r1, r1, r1
    .size callee, .-callee
    .size _start, .-_start

# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK:        10000: {{.*}} lui{{.*}}r0,
# CHECK:        1000c: {{.*}} addi32{{.*}}r0,{{.*}}r0,
# CHECK:        10018: {{.*}} jalr{{.*}}r0,{{.*}}r0
# CHECK-NOT: r12
# CHECK-NOT: xor32
# CHECK-NOT: lui_w
# CHECK-NOT: jalr
