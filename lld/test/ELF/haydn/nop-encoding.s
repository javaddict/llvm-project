# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -x .text %t | FileCheck --check-prefix=HEX %s
# RUN: not --crash llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym PARTIAL=1 -o %t.bad.o 2>&1 | FileCheck --check-prefix=PARTIAL %s
#
# REGRESSION: under format E a NOP bundle is NOT an all-zero word. Inst{2-0}
# is the 0b111 format indicator and Inst{3} the entry count, so twelve zero
# bytes are a different format's bundle, not a nop
# (FORMAT-E-SWITCH-PLAN.md § 3, § 5.2). Bundle128 had no header and a zero
# parcel WAS the nop, which is what this test used to assert.
#
# The other half of the old test is gone rather than ported. It forced
# `.p2align 5` to produce one all-zero pad parcel; a format E parcel is 12
# bytes, which is not a power of two, so .p2align can never request a whole
# number of them except by requesting none. That is why function alignment is
# Align(4) (§ 5.9). The PARTIAL case below pins the refusal instead.

.ifdef PARTIAL
.section .text
.globl partial
partial:
    nop
    # 32-byte alignment after one 12 B bundle -> 20 bytes, neither zero nor a
    # whole number of parcels. writeNopData must refuse rather than emit a
    # fragment of a bundle.
    .p2align 5
    ADD32 R2, R2, R2
# PARTIAL: unable to write nop sequence
.else
.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # An explicit nop survives linking and still decodes as a nop bundle.
    # CHECK: 10000: {{.*}} nop
    nop

    # Already satisfied at every bundle boundary, so this emits nothing and
    # the next instruction lands at 0xc — one parcel on, not one 16-byte
    # parcel on.
    .p2align 2
    # CHECK: 1000c: {{.*}} add32
    ADD32 R2, R2, R2

    .size _start, .-_start

# The load-bearing assertion: no parcel in .text is all zeros. Deliberately
# negative — asserting the nop's actual bytes would be asserting what the
# encoder did, and writeNopData's all-zero payload is still the open § 5.2
# FIXME. What is spec, and what this pins, is that a zero word is not a nop.
# HEX: Hex dump of section '.text':
# HEX-NOT: 00000000 00000000 00000000
.endif
