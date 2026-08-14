# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: MC — generated Format E members are not in the public matcher
# (`AsmVariantName = e96member`). Dual logicals still assemble; textual
# position owns E2 entry. A member match would fail closed ("private
# placement opcode").
#
# Residual `_S*` FieldSlots use AsmVariantName fieldslot (not in the
# public matcher). Catalog logicals and ld32_reg shells match instead.
# A FieldSlot or generated-member match fails closed ("private
# placement opcode"); the parser does not peel `_S*`.

# CHECK: add32
# CHECK: xor32
{ add32 r1, r0, r2; xor32 r3, r0, r4 }

# CHECK: abs64
# CHECK: neg64
{ abs64 d0, d1; neg64 d2, d3 }

# CHECK: zero_gpr
{ zero_gpr r1; nop }

# CHECK: csrw
{ csrw 1, r1; nop }

# CHECK: add64s
# CHECK: zero_dr
{ add64s d0, d1, d2; zero_dr d3 }
