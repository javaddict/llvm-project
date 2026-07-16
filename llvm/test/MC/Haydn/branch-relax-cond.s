# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
#
# Bundle128 conditional branches: imm12 (÷2) range is ±4 KiB. Longer gaps
# must go through LLD thunks (lld/test/ELF/haydn/reloc-long-branch-thunk.s)
# not MC applyFixup. Keep a mid-range gap that still assembles+disassembles.

.text
.globl _start
_start:

# CHECK-LABEL: <test_beq_mid>:
test_beq_mid:
# CHECK: beq
    beq r1, r2, mid_target1
    .space 2000, 0
mid_target1:
# CHECK: nop
    nop

# CHECK-LABEL: <test_bnez_mid>:
test_bnez_mid:
# CHECK: bnez
    bnez r5, mid_target3
    .space 2000, 0
mid_target3:
    nop
