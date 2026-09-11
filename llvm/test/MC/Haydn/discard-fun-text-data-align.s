# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: llvm-readobj -x .text %t.o | FileCheck %s --check-prefix=HEX
# REQUIRES: haydn-registered-target

# Role: object — NatureDSP DISCARD_FUN is two 8-byte @object stubs in the
# default text section with `.align 4` (Haydn: 2^4 = 16). That requests an
# 8-byte remainder after the first stub. writeNopData must not invent a
# 4/8-byte idle packet. Data-only text zero-fills; instruction text still
# refuses (a6-bundle128-pad-only.s NEG).

.text
.type stub_a, @object
.globl stub_a
.align 4
stub_a:
  .long 0x49438B96, 0x4D73F192

.type stub_b, @object
.globl stub_b
.align 4
stub_b:
  .long 0x49438B96, 0x4D73F192

# 8-byte stub + 8-byte zero pad + 8-byte stub = 24.
# SEC: Name: .text
# SEC: Size: 24

# HEX: 0x00000000 968b4349 92f1734d 00000000 00000000
# HEX-NEXT: 0x00000010 968b4349 92f1734d
