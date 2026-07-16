# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: R_HAYDN_CallSImm20 must preserve JAL opcode under Bundle128
# (linear imm20 at s0 bits[23:4], FieldLsb=4 / CB-82). Corrupted patches
# disassemble as <unknown> or LUI.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_CallSImm20 callee 0x0
# RELOCS-DAG:      0x10 R_HAYDN_CallSImm20 callee 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal{{.*}}lr,
    jal lr, callee

    # CHECK: 10010: {{.*}} jal{{.*}}r0,
    jal r0, callee

    # CHECK: 10020: {{.*}} add32
    ADD32 R0, R0, R0

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    ADD32 R1, R1, R1
    .size callee, .-callee

    .size _start, .-_start
