# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t --section-start=.text=0x10000 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: there is no call veneer whose register
# discipline could be pinned (the retired one borrowed soft-zero R0 and
# left the JALR link there). The far call over the 1.25 MB gap is now an
# explicit link error naming the veneer ABI gap; no thunk bytes that could
# touch R0–R7 or R12 are emitted. The R1–R7 loads stay as the site shape.

.section .text
.globl _start
_start:
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
    add32 r1, r1, r1
    .size callee, .-callee
    .size _start, .-_start

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_CallSImm20 to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
