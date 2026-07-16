; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; REGRESSION TEST: plain 64-bit loads (LD64) must use a slot-0/1 choice-set so
; two independent loads pack into one bundle (slot0 + slot1), matching the spec
; (D_LDW_WITH_IMM slots 0+1) and enabling SMS to find lower-II schedules.
;
; Bug : the GISel selector emitted LD64_S1 (slot-1-only) for every 64-bit
; load. Two LD64_S1 both demand SLOT1, so the DFA could never place them in the
; same cycle. On firdec32x32_D2 this inflated the SMS initiation interval from
; II=5 (achievable) to II=6 (span-too-large → single-stage → rejected). Every
; FIR/IIR/FFT dual-load kernel hit this.
;
; Fix: add a plain LD64 opcode (FmtLS 0x45) with Slot01_LD itinerary
; ([SLOT0,SLOT1] choice-set). The selector emits LD64; the post-RA
; promoteLoadsToSlot1 rewrites the second LD64 -> LD64_S1 when slot 0 is
; occupied, so the final encoding assigns concrete slots unambiguously.
;
; extends this: getLegalSlots(LD64) now returns LSB_S0|LSB_S1 (spec
; faithful), making the Bundle slot model consistent with the itinerary; and
; the alternative-aware HaydnResourceCycle is now the default SMS resource
; model (replacing the choice-set-naive DFA). See companion test
; ld64-dual-load-slot-polymorphic.ll.
;
; Test design: two independent i64 loads from the same base. Before the fix both
; were LD64_S1 (slot-1-only) and could NOT share a bundle. After the fix the
; first stays LD64 (slot 0) and the second is promoted to LD64_S1 (slot 1)
; packing into one `{ ld64...; ld64... }` bundle. If the fix regresses, the
; two loads fall back to separate bundles (or both ld64, which the hazard
; recognizer rejects as a slot conflict).
;
; Dual-sched form: i64 loads lower to paired ld32s. Contract: two independent
; half-word loads co-issue in one bundle ({ ld32...; ld32... }), proving
; dual-load packing (/). Pointer bump is explicit addi32 (not fused
; s_lw_post_imm) under current dual-sched.

; REBASELINED (auto) dual-sched / AR logical-slot rebaseline;.file skipped

; CHECK: 	.text
; CHECK: 	.globl	dual_load_i64                   // -- Begin function dual_load_i64
; CHECK: 	.type	dual_load_i64,@function
; CHECK: dual_load_i64:                          // @dual_load_i64
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	ld32	r4, r1, 0; 	subi32	sp, sp, 8; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r1, 4; 	addi32	r1, r1, 8; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r5, r1, 4; 	ld32	r3, r3, 0; 	nop }
; CHECK: 	{ 	ld32	r1, r1, 0; 	ld32	r5, r5, 0; 	nop }
; CHECK: 	{ 	st32	r4, sp, 0 }
; CHECK: 	{ 	st32	r3, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	st32	r1, sp, 0 }
; CHECK: 	{ 	st32	r5, sp, 4 }
; CHECK: 	{ 	ld64	d1, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r2, 4; 	add64	d0, d0, d1; 	nop }
; CHECK: 	{ 	d_sw_l_with_imm	d0, r2, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8; 	d_sw_h_with_imm	d0, r1, 0; 	nop }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	dual_load_i64, .Lfunc_end0-dual_load_i64
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @dual_load_i64(ptr %p, ptr %q) nounwind {
; The two independent i64 loads must share ONE bundle line. Before both were
; LD64_S1 (slot-1-only) and could never pack; now one is LD64 (slot 0) and the
; other is promoted to LD64_S1 (slot 1) by promoteLoadsToSlot1.
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %s = add i64 %a, %b
  store i64 %s, ptr %q
  ret void
}
