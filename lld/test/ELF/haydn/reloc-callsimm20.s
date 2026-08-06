# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: R_HAYDN_WIDE_CallSImm20 must preserve JAL under Format E
# (EncodedBytes=12). Corrupted patches disassemble as <unknown> or LUI.
# Product call reloc is WIDE_CallSImm20 (not retired non-WIDE CallSImm20).

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x0 R_HAYDN_WIDE_CallSImm20 callee 0x0
# RELOCS-DAG:      0xC R_HAYDN_WIDE_CallSImm20 callee 0x0
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
