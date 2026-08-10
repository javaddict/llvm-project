# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=POS
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym NEG=1 -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=NEG
# REQUIRES: haydn-registered-target

# Role: object — Format E pad/alignment fail-closed: executable pad must be
# whole EncodedBytes (12) parcels. Do not invent idle/pad byte values (golden
# idle completion still unspecified). Positive path only checks that aligned
# nop+ADD32 assembles to a decodable ADD32 parcel; negative path checks
# writeNopData refuses non-multiples of EncodedBytes.

.ifndef NEG
.section .text
.globl _start
_start:
    nop
    .p2align 2
    ADD32 R2, R2, R2
    .size _start, .-_start

# POS: {{.*}}add32{{.*}}r2, r2, r2
# Fail-closed: do not CHECK-claim all-zero product-NOP or idle header bytes.
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
