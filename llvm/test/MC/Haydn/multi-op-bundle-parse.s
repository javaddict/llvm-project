# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: multi-op `{ op1; op2; op3 }` VLIW bundle parsing.
#
# B3.7: AsmParser mirrors AIE (AIEBaseAsmParser.h:164-211):
#   Bundle.canAdd/add per matched child (fail-closed "incorrect bundle")
#   emitBundle: getFormatOrNull → Format->Opcode (product BUNDLE128_FULL)
#   S0-S1-S2 encode order with NOP fill (HaydnAsmPrinter.cpp:481-527 peer)
#
# Deleted vs pre-B3.7: source-order BundleChildren + setOpcode(Haydn::BUNDLE);
# count-only "bundle exceeds 3" legality (superseded by Bundle.canAdd).
#
# Placement: S2→S1→S0 tryAdd for multi-slot logicals (HaydnBundleFormatSolver).
# Disasm prints S0-S1-S2 with `nop` for idle slots. Textual source order may
# differ from printed slot order.
# (-z: Bundle128 leading-zero-skip.)
#
# Spec reference: encoding_manual_flex.md §1 (3 slots per bundle).

.text

# CHECK-LABEL: <.text>:

# Single-op brace bundle: canAdd/add → SlotMap; emit BUNDLE128_FULL with NOPs.
# Lone ALU64 prefers S2; print is s2-s1-s0 → `{ add64...; nop; nop }`.
# CHECK: { add64 d0, d1, d2; nop; nop }
{ add64 d0, d1, d2 }

# Two-op right-aligned text: add64 names s1 and add32 names s0, both legal
# where written. (Under the old S0-first spelling this same line packed
# add64->S2 / add32->S1; the ops are simply written in ISA slot order now.)
# CHECK-NEXT: { nop; add64 d0, d1, d2; add32 r0, r1, r2 }
{ add64 d0, d1, d2; add32 r0, r1, r2 }

# Three-op: text is s2-s1-s0, and every op is legal where it is written.
# CHECK-NEXT: { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
{ add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
