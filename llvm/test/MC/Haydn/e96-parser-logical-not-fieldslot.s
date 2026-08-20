# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=FIELDSLOT=1 %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=FIELDSLOT
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=MEMBER=1 %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=MEMBER
# REQUIRES: haydn-registered-target

# Role: MC — generated Format E members are not in the public matcher
# (`AsmVariantName = e96member`). Dual logicals still assemble; textual
# position owns E2 entry. A member match would fail closed ("private
# placement opcode").
#
# Residual `_S*` FieldSlots use AsmVariantName fieldslot (not in the
# public matcher). Catalog logicals and ld32_reg shells match instead.
# A FieldSlot or generated-member mnemonic is refused before match
# ("private placement opcode"); the parser does not peel `_S*`.

.ifdef FIELDSLOT
# FIELDSLOT: error: assembler matched a private placement opcode
add32_s0 r1, r0, r2
.else
.ifdef MEMBER
# MEMBER: error: assembler matched a private placement opcode
add32_e2_e0_alu0_rr r1, r0, r2
.else

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
.endif
.endif
