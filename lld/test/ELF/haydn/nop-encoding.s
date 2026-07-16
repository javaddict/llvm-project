# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -x .text %t | FileCheck --check-prefix=HEX %s
#
# REGRESSION: Bundle128 NOP is the all-zero 16-byte parcel (idle slot windows).
# Explicit `nop` and linker alignment padding must decode as nop, not garbage.

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: {{.*}} nop
    nop

    # Force alignment to 32 bytes → one all-zero Bundle128 pad after 16-byte nop.
    .p2align 5
    # CHECK: {{.*}} add32
    ADD32 R2, R2, R2

    .size _start, .-_start

# First parcel all zeros (explicit nop or pad), second is ADD32.
# HEX: Hex dump of section '.text':
# HEX: 00000000 00000000 00000000 00000000
