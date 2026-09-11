# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: not ld.lld %t.o -o %t --section-start=.text=0x10000 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: the retired call veneer wrote
# LUI + ADDI32 + JALR on soft-zero R0 (3 × production EncodedBytes at
# offsets 0 / N / 2N, Align-4). That byte-emission contract is gone: the
# far call is an explicit link error naming the veneer ABI gap, and no
# R0-writing thunk bytes appear in any output. Veneer geometry belongs to
# the ISA-70 template when one is approved.

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

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_CallSImm20 to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
