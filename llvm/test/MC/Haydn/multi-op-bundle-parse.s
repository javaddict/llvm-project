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

# High-entry-first text (CB-142 / #10). Single real → e0 with high nop pad.
# CHECK: {{.*}}0: 07 0b 04 21 00 00 00 00 00 00 00 00 { nop; add64 d0, d1, d2 }
{ add64 d0, d1, d2 }

# Two-op high-first: first text → high entry. Encoder may E2→E3-pad.
# CHECK-NEXT: c: 4f 49 80 10 a0 04 41 08 00 00 00 00 { add64 d0, d1, d2; add32 r0, r1, r2 }
{ add64 d0, d1, d2; add32 r0, r1, r2 }

# Three-op: text order is e2;e1;e0 print order (high first).
# CHECK-NEXT: {{.*}}18: 4f 49 80 10 a0 04 0d 15 e0 82 20 04 { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
{ add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }

# Three-op rematch source: still high-first print of encoded e2;e1;e0.
# CHECK-NEXT: {{.*}}24: 4f 09 1a 2a a0 04 41 08 e0 12 20 04 { add32 r0, r1, r2; add64 d0, d1, d2; add64 d3, d4, d5 }
{ add32 r0, r1, r2; add64 d0, d1, d2; add64 d3, d4, d5 }
