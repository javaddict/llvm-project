# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s

// REGRESSION TEST (D177 / L145 / F1): Long-call thunk must use R12 (AT), NOT R1.
//
// Bundle128 thunk (post D456): LUI r12 + ADDI32 r12,r12 + JALR r0,r12 — three
// 16-byte parcels (48 bytes). Touches only R12 among GPRs.
//
// Comment-string note: Haydn AsmParser CommentString is "//".

// Thunk first (low address).
# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK: lui{{.*}}r12,
# CHECK: addi32{{.*}}r12,{{.*}}r12,
# CHECK: jalr{{.*}}r0,{{.*}}r12
# CHECK-NOT: lui{{.*}}r1,

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: addi32{{.*}}r1,{{.*}}r0,{{.*}}42
    addi32 r1, r0, 42
    addi32 r2, r0, 43
    addi32 r3, r0, 44
    addi32 r4, r0, 45
    addi32 r5, r0, 46
    addi32 r6, r0, 47
    addi32 r7, r0, 48

    jal lr, callee

    .space 0x140000

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    add32 r1, r1, r1
    .size callee, .-callee

    .size _start, .-_start
