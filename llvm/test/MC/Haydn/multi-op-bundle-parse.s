# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: multi-op `{ op1; op2; op3 }` VLIW bundle parsing.
#
# Bug surface: until the Haydn asm parser split a `{ a; b; c }` line into
# N standalone MCInsts (each emitting its own parcel). The MC-layer contract
# (HaydnAsmPrinter, HaydnDisassembler, HaydnMCCodeEmitter::encodeBundle) is
# that a VLIW bundle is ONE Haydn::BUNDLE MCInst whose operands are
# MCOperand::createInst(child) per real slot. The parser was the only producer
# NOT honoring that contract — it emitted children inline via the streamer.
#
# Fix : parseInstruction now accumulates the per-instruction children
# allocates each via MCContext::createMCInst, and emits ONE Haydn::BUNDLE
# MCInst with N createInst children. encodeBundle (HaydnMCCodeEmitter.cpp)
# iterates these operands and packs them into a single Bundle128 parcel;
# printInst (HaydnInstPrinter.cpp) renders them back as `{ a; b; c }`.
#
# Post cutover: multi-op Bundle128 packing IS implemented. The encoder
# routes ops to slot windows via the FlexMap slot authority; the decoder
# renders slots in s0/s1/s2 order with `nop` for idle slots. The textual
# order in the asm source may differ from the slot order in disasm.
# (-z: Bundle128 leading-zero-skip per.)
#
# This test pins the PARSER-side contract: the asm `{ a; b }` MUST produce a
# single bundle that round-trips back as the same `{ a; b }` text via objdump.
# A regression that re-splits the children would either:
# (a) emit two standalone parcels (objdump shows two separate lines, no `{ }`)
# (b) mis-print because no Haydn::BUNDLE MCInst was formed at all.
#
# Spec reference: encoding_manual_flex.md §1 (3 slots per bundle).
# Related: / (Bundle128 single-op), (parser multi-op — this test)
# (single Bundle128 FlexMap slot authority — multi-child packing).

.text

# CHECK-LABEL: <.text>:

# Single-op bundle: 1 child wrapped in Haydn::BUNDLE. Round-trips as
# `{ nop; add64...; nop }` — the lone ALU64 op routes to slot 1.
# CHECK: { nop; nop; add64 d0, d1, d2 }
{ add64 d0, d1, d2 }

# Two-op bundle: 2 children in Haydn::BUNDLE. add32 routes to s0, add64 to s1
# (slot order, not textual order). Round-trips as `{ a; b; nop }`.
# CHECK-NEXT: { nop; add32 r0, r1, r2; add64 d0, d1, d2 }
{ add64 d0, d1, d2; add32 r0, r1, r2 }

# Three-op bundle: 3 children (the spec max — ISSUE_SLOT_COUNT). All three
# slot windows occupied; no idle nop.
# CHECK-NEXT: { add32 r0, r1, r2; add64 d3, d4, d5; add64 d0, d1, d2 }
{ add64 d0, d1, d2; add32 r0, r1, r2; add64 d3, d4, d5 }
