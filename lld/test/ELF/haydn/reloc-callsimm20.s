# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: R_HAYDN_WIDE_CallSImm20 must preserve JAL opcode under Bundle128
# (linear imm20 at s0 bits[23:4], FieldLsb=4 / CB-82). Corrupted patches
# disassemble as <unknown> or LUI.

# Neither the offset nor the addend is a constant of this test; both follow
# from where the packer put the jal. The offset is bundle_start + the entry's
# byte base, and the addend is the same byte base again, because a branch is
# resolved from the BUNDLE and not from the entry it happens to sit in
# (`14afcf2e79a5`). So an entry-1 jal at bundle 0 reads 0x4 / 0x4, and the two
# cancel. The invariant is that they are equal and that the linked target is
# exactly `callee` — checked below by address, which is the part that would
# actually be wrong if this drifted.
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x4 R_HAYDN_WIDE_CallSImm20 callee 0x4
# RELOCS-DAG:      0x10 R_HAYDN_WIDE_CallSImm20 callee 0x4
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal{{.*}}lr,
    jal lr, callee

    # CHECK: 1000c: {{.*}} jal{{.*}}r0,
    jal r0, callee

    # CHECK: 10018: {{.*}} add32
    ADD32 R0, R0, R0

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    ADD32 R1, R1, R1
    .size callee, .-callee

    .size _start, .-_start
