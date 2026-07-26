# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=HEX
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym NEG=1 -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=NEG
#
# A.6: executable alignment pad is Bundle128 parcels only (16 B all-zero).
# Positive: .p2align 5 after one nop → one full 16 B zero pad parcel.
# Negative: non-16-byte pad request aborts writeNopData.

.ifndef NEG
.section .text
.globl _start
_start:
    nop
    # 32-byte align after 16 B nop → one full Bundle128 pad.
    .p2align 5
    ADD32 R2, R2, R2
    .size _start, .-_start

# Two all-zero 16 B parcels (nop + pad), then ADD32.
# HEX: Hex dump of section '.text':
# HEX: 0x00000000 00000000 00000000 00000000 00000000
# HEX: 0x00000010 00000000 00000000 00000000 00000000
.else
.section .text
.globl bad
bad:
    nop
    .byte 0
    .p2align 2
    ADD32 R1, R1, R1
# NEG: unable to write nop sequence
.endif
