# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -x .text %t | FileCheck --check-prefix=HEX %s
#
# Explicit `nop` still comes from the assembler (MC product path).
# LLD executable fill is whole production EncodedBytes only; partial 2/4-byte
# linker NOPs are retired. Product idle parcel is Format E header 0x07 +
# zero entries (12 B), not an all-zero 16-byte Bundle128 word.

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: {{.*}} nop
    nop

    # CHECK: {{.*}} add32
    ADD32 R2, R2, R2

    .size _start, .-_start

# Product Format E idle/NOP parcel (GE96-01 provisional header 0x07).
# HEX: Hex dump of section '.text':
# HEX: 07000000 00000000 00000000
