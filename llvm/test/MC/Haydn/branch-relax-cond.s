# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s

# Role: object — mid-range conditional branch assemble→obj→disasm smoke.
# Branch PC-rel wire scale is not golden-defined; do not pin ÷2 / halfword field
# values as product law. This lit only requires the mnemonic to disassemble for
# a mid-range gap that currently assembles. Longer gaps go through LLD thunks
# (lld/test/ELF/haydn/reloc-long-branch-thunk.s), not MC applyFixup.
# Positive in-range scale oracles remain residual (see reloc-range-branch-div2).

.text
.globl _start
_start:

# CHECK-LABEL: <test_beq_mid>:
test_beq_mid:
# CHECK: beq
    beq r1, r2, mid_target1
    .space 2016, 0
mid_target1:
# CHECK: nop
    nop

# CHECK-LABEL: <test_bnez_mid>:
test_bnez_mid:
# CHECK: bnez
    bnez r5, mid_target3
    .space 2016, 0
mid_target3:
    nop
