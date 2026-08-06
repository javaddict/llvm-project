# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-nm %t | FileCheck --check-prefix=NM %s
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Call veneer uses soft-zero R0 only — never R1–R7 (args) or R12.
# Geometry: 3 × production EncodedBytes.

# NM: __haydn_thunk_callee

# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK-NOT: lui{{.*}}r1,
# CHECK-NOT: r12

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
