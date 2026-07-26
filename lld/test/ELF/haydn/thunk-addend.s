# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOC %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# Call veneer honors addend (callee+16).

# RELOC: R_HAYDN_CallSImm20 callee 0x10

.section .text
.globl _start
_start:
    jal lr, callee + 16
    .space 0x140000

.globl callee
callee:
    add32 r3, r3, r3
    .space 12
.globl skip_pad
skip_pad:
    add32 r4, r4, r4
    .size callee, .-callee
    .size _start, .-_start

# CHECK-LABEL: <__haydn_thunk_callee>:
# CHECK: lui{{.*}}r0,
# CHECK: addi32{{.*}}r0,{{.*}}r0,
# CHECK: jalr{{.*}}r0,{{.*}}r0
# CHECK-NOT: xor32
# CHECK-LABEL: <skip_pad>:
# CHECK: add32{{.*}}r4
