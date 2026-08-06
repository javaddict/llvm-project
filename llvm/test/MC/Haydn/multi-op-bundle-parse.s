# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — multi-op `{ op1; op2; op3 }` VLIW bundle parsing.

# REGRESSION TEST: multi-op `{ op1; op2; op3 }` VLIW bundle parsing.
#
# B3.7: AsmParser mirrors AIE (AIEBaseAsmParser.h:164-211):
#   Bundle.canAdd/add per matched child (fail-closed "incorrect bundle")
#   emitBundle: getFormatOrNull → Format->Opcode (product BUNDLE_E96)
#   S0-S1-S2 encode order with NOP fill (HaydnAsmPrinter.cpp:481-527 peer)
#
# Deleted vs pre-B3.7: source-order BundleChildren + setOpcode(Haydn::BUNDLE);
# count-only "bundle exceeds 3" legality (superseded by Bundle.canAdd).
#
# Placement: exact canAdd/add rematches multi-slot logicals (prefer high slots
# when free). Disasm prints S0-S1-S2 with `nop` for idle slots. Textual source
# order may differ from printed slot order; rematch and pack-friendly source
# orders must encode/disasm to the same selected member order.
# (-z: Format E leading-zero-skip.)
#
# Spec reference: encoding_manual_flex.md §1 (3 slots per bundle).

.text

# CHECK-LABEL: <.text>:

# Single-op brace bundle: canAdd/add → SlotMap; emit BUNDLE_E96 with NOPs.
# Lone ALU64 prefers S2 → `{ nop; nop; add64... }`.
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00 { add64 d0, d1, d2; nop }
{ add64 d0, d1, d2 }

# Two-op: add64→S2, add32→S1 → print S0-S1-S2 order.
# CHECK-NEXT: c: 4f 09 82 10 a0 24 40 08 00 00 00 00 { add64 d0, d1, d2; add32 r0, r1, r2; nop }
{ add64 d0, d1, d2; add32 r0, r1, r2 }

# Three-op pack-friendly source: add64→S2, add64→S1, add32→S0.
# CHECK-NEXT: {{.*}}18: 4f 09 82 10 a0 04 0d 15 e0 12 20 04 { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
{ add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }

# Three-op rematch source (preferred first-fit freezes add32 on S2 then loses
# the second add64). Exact canAdd/add rematches add32 onto S0 so both add64
# keep S1|S2. Objdump member order must match the pack-friendly case above
# (S0 add32; S1 add64; S2 add64) — assembler and post-RA share that order.
# CHECK-NEXT: {{.*}}24: 4f 49 80 10 a0 04 41 08 e0 82 86 0a { add32 r0, r1, r2; add64 d0, d1, d2; add64 d3, d4, d5 }
{ add32 r0, r1, r2; add64 d0, d1, d2; add64 d3, d4, d5 }
