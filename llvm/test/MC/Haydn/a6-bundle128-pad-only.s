# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SIZE
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym NEG=1 -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=NEG
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym NEG32=1 -o %t.bad32.o 2>&1 | FileCheck %s --check-prefix=NEG32
#
# A.6: an executable alignment pad is whole parcels only.
#
# Under format E a parcel is 12 bytes, which is NOT a power of two, so
# .p2align can never request a whole number of them except by requesting none.
# That is why function alignment is Align(4) and not the parcel size: every
# bundle boundary is at section_start + 12k, which is always 4-aligned, so the
# contract is already satisfied and the padding is always zero
# (FORMAT-E-SWITCH-PLAN.md § 5.9). Bundle128 could ask for Align(16) and get a
# whole 16-byte pad parcel; that positive case has no format E counterpart.
#
# Deliberately asserts no pad CONTENT. writeNopData's all-zero payload is still
# wrong under format E — twelve zero bytes are not a NOP bundle, because
# Inst{2-0} is the 0b111 format indicator — and is marked FIXME rather than
# quietly resized (§ 5.2). Nothing here asks it for a non-zero length, so the
# defect is out of reach; asserting bytes now would freeze the wrong answer.

.ifdef NEG
# Not a whole parcel: one 12 B bundle plus a stray byte, then a 4-byte
# alignment request -> 3 bytes of padding.
.section .text
.globl bad
bad:
    nop
    .byte 0
    .p2align 2
    ADD32 R1, R1, R1
# NEG: unable to write nop sequence
.else
.ifdef NEG32
# 32-byte alignment after one 12 B bundle -> 20 bytes of padding, which is
# neither zero nor a whole number of parcels.
.section .text
.globl bad32
bad32:
    nop
    .p2align 5
    ADD32 R2, R2, R2
# NEG32: unable to write nop sequence
.else
# Positive: .p2align 2 is always already satisfied at a bundle boundary, so it
# emits nothing at all and .text is exactly two 12 B parcels.
.section .text
.globl _start
_start:
    nop
    .p2align 2
    ADD32 R2, R2, R2
    .size _start, .-_start

# SIZE: Name: .text
# SIZE: Size: 24
.endif
.endif
