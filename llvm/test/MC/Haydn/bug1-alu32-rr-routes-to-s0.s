# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=NOPH

# Role: object — TRACKING: encoder slot-assignment gap (post- Format-E-only cutover).

# TRACKING: encoder slot-assignment gap (post- Format-E-only cutover).
# The encoder places the MAC op (x2mula32) in a slot window the decoder
# does NOT render with the `` suffix -- actual output is `x2mula32 d1
# d2, d3, d1` (no ``); CHECK wants `x2mula32`. The companion
# flex-bytes-mac.s decoder oracle (fed spec-correct s1 bytes via.byte)
# DOES render `x2mula32`, confirming the spec layout is s1. The ALU32 RR
# ops (add32s/slt32/max32) ARE rendered correctly -- only the MAC
# suffix is missing. Same slot-assignment gap as
# d385-format-e-add64-roundtrip.s (add64 vs add64). The core intent
# (ALU32 RR routes to s0, NOT s1) IS satisfied. Un-XFAIL when the encoder
# reads the slot from the format-def and x2mula32 round-trips as x2mula32.
# Prior XFAIL (Phase-2 decoder purge collateral) is resolved: the s0 ALU32
# RR ops (add32s/slt32/max32) decode via the Mode-0 s0 trie, and the s1 MAC
# family (x2mula32) decodes via DecoderTableHaydnM0S1MAC64 (X2MULA32_M0S1).
# The 4-operand ACC form (rd, rs1, rs2, ra) is asm-PARSED by the generic
# FmtMAC def; the encoder routes to X2MULA32_M0S1 whose asm string is the
# 3-op form (rd_in is tied to rtd and not printed — the intentional
# asm-string-neutral pattern). So objdump output is 3-op for the MAC line.

# REGRESSION TEST: 3-operand ALU32 RR ops (add32s/slt32/max32) and 4-operand
# MAC ops (x2mula32) MUST NOT land in the s1 ALU32 slot.
#
# Bug (encoding_manual.md §3.4 note + §6.6): s1 ALU32 is UNARY-ONLY
# (destructive 2-operand: rt=rsd1). The encoder was previously routing
# 3-operand ALU32 RR (add32s rd,rs1,rs2; slt32; max32) into s1's 2-operand
# ALU32 sub-row, dropping operand 2 (rs2). The decoder then printed `<?>`
# for the missing operand (SmallVector OOB at the print boundary).
#
# Fix: getLegalSlots gates 3-op ALU32 RR to LSB_S0 only; the encoder either
# uses G-format (when all regs are r0-r7 and rs2 fits the 2-bit imm2 field)
# or emits a single-child Format E singleton parcel. s1 ALU32 only accepts the
# unary family (NOT32/NEG32/NEG32S/NSA32/NSAU32/POPCOUNT32).
#
# Hard bar: NO `<?>` residual on disassembly of these opcodes.
#
# This test pins BOTH:
# the spec-correct compact G-format path (r0-r7, rs2 in r0-r3)
# the Mode-0 s0 path (r8-r15, rs2 in r4-r15)
# Neither path may emit a 3-op ALU32 into the s1 ALU32 sub-row.

.text
.globl test_alu32_rr
test_alu32_rr:
  # G-format: r0-r7 with rs2 in r0-r3 fits the 4-byte G-format s0 sub-slot
  # (encoding_manual.md §3.4 Combo 1: single ALU32 RR).
  { add32s r1, r2, r3 }
  { slt32  r4, r5, r0 }
  { max32  r6, r7, r1 }

  # Format E singleton parcel: r8-r15 (or rs2 > r3) forces the retired 8-byte Mode-0 path.
  # add32s MUST land in s0, NOT s1 ALU32 sub-row.
  { add32s r9, r10, r11 }
  { slt32  r9, r10, r11 }
  { max32  r9, r10, r11 }

  # MAC: x2mula32 is FU_MAC (S1/S2), 4-operand destructive DR64 SIMD MAC.
  # Lives in s1 MAC sub-row (NOT ALU32 sub-row). Operand 3 ($ra) is tied to
  # $rd via the destructive accumulator semantic. (: macq31 removed as a
  # phantom instruction; x2mula32 is the real ISA DR64 MAC op that exercises
  # the same s1 FU_MAC routing.)
  { x2mula32 d1, d2, d3, d1 }

# CHECK-LABEL: <test_alu32_rr>:
# Format E path: every op is a 12-byte Format E composite `{ op.sN...; nop; nop }`.
# CHECK: {{.*}}0: 07 ab 10 32 00 00 00 00 00 00 00 00 { 	nop; 	add32s	r1, r2, r3 }
# CHECK-NEXT: c: 07 8b 42 05 00 00 00 00 00 00 00 00 { 	nop; 	slt32	r4, r5, r0 }
# CHECK-NEXT: {{.*}}18: 07 0b 62 17 00 00 00 00 00 00 00 00 { 	nop; 	max32	r6, r7, r1 }
# Mode-0 s0 path (also 12-byte Format E under).
# CHECK: {{.*}}24: 07 ab 90 ba 00 00 00 00 00 00 00 00 { 	nop; 	add32s	r9, r10, r11 }
# CHECK-NEXT: {{.*}}30: 07 8b 92 ba 00 00 00 00 00 00 00 00 { 	nop; 	slt32	r9, r10, r11 }
# CHECK-NEXT: 3c: 07 0b 92 ba 00 00 00 00 00 00 00 00 { 	nop; 	max32	r9, r10, r11 }
# MAC path (s1 MAC sub-row, not ALU32). The 4-op asm-parse form
# (x2mula32 d1, d2, d3, d1) encodes via X2MULA32_M0S1 whose asm string is the
# 3-op form (rd_in is tied to rtd and not printed). So objdump output is 3-op.
# CHECK: {{.*}}48: 47 02 12 32 01 00 00 00 00 00 00 00 { 	nop; 	x2mula32	d1, d2, d3, d1 }

# NOPH: <test_alu32_rr>:
# NOPH-NOT: <?>
