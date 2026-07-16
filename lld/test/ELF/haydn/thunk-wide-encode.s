# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION TEST: long-call thunk must emit Bundle128 LUI + ADDI32 + JALR
# (3 × 16-byte parcels = 48 bytes), NOT multi-width LUI_W+JALR_W (12 bytes).
#
# Sequence (R12 = AT scratch, D177):
#   0x10000: lui   r12, hi12(target)
#   0x10010: addi32 r12, r12, lo20(target)
#   0x10020: jalr  r0, r12, 0
#
# Load-bearing: second parcel at +0x10 and third at +0x20 (16-byte spacing).
# A regression to 6-byte WIDE parcels would put jalr at +0x6 and decode as
# <unknown>. Do NOT re-accept lui_w / jalr_w multi-width forms.

.section .text
.globl _start
_start:
    # Out-of-range call: callee is >1MB away.
    jal lr, callee

    .space 0x140000

.globl callee
callee:
    add32 r1, r1, r1
    .size callee, .-callee

    .size _start, .-_start

# Thunk at low end of .text (0x10000).
# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK:        10000: {{.*}} lui{{.*}}r12,
# CHECK:        10010: {{.*}} addi32{{.*}}r12,{{.*}}r12,
# CHECK:        10020: {{.*}} jalr{{.*}}r0,{{.*}}r12
# Multi-width negative guards:
# CHECK-NOT: lui_w
# CHECK-NOT: jalr_w
