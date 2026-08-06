# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=HEX
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym NEG=1 -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=NEG

# Role: object — Format E: executable pad is whole EncodedBytes (12) idle
# parcels only (GE96-01 provisional idle header 0x07 + zero entries).

# Positive: after one 12 B nop, .p2align 2 is already satisfied (12 ≡ 0 mod 4)
# → zero pad bytes; object is a single Format E idle parcel then ADD32.
# Negative: residual .byte breaks 12 B alignment; writeNopData refuses
# non-multiples of EncodedBytes.

.ifndef NEG
.section .text
.globl _start
_start:
    nop
    .p2align 2
    ADD32 R2, R2, R2
    .size _start, .-_start

# Idle parcel (07 00..) then ADD32 Format E parcel.
# HEX: Hex dump of section '.text':
# HEX: 0x00000000 07000000 00000000 00000000
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
