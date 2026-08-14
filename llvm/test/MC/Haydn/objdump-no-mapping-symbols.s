# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -s %t.o | FileCheck %s
#
# Role: object — T-MC9 mapping-symbol residual. Haydn ELF ABI has no ARM/RISCV
# mapping-symbol scheme ($a/$d/$t/$x). Do not invent one. This file pins the
# current ABI: ordinary STT_FUNC / STT_OBJECT names only.
#
# Residual: T-MC9 mapping symbols remain OPEN until an ABI document defines
# them. Mid-parcel symbols silently re-grid decode (maturity scorecard).

.text
.globl mapped_fn
.type mapped_fn,@function
mapped_fn:
  { xor32 r0, r0, r0; nop; nop }
.size mapped_fn, .-mapped_fn

.data
.globl mapped_data
.type mapped_data,@object
mapped_data:
  .byte 0x42, 0x43, 0x44, 0x45
.size mapped_data, 4

# CHECK: mapped_fn
# CHECK: mapped_data
# CHECK-NOT: {{[[:space:]]\$a$}}
# CHECK-NOT: {{[[:space:]]\$d$}}
# CHECK-NOT: {{[[:space:]]\$t$}}
# CHECK-NOT: {{[[:space:]]\$x$}}
