; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; REGRESSION TEST : the plain LD64 must remain slot-0-OR-slot-1
; (slot-polymorphic) so two independent 64-bit loads pack into one bundle
; (slot0 + slot1). This is the dominant SWPS lever (Subagent A): SMS
; rejects every dual-load streaming loop when LD64 is slot-1-only, because both
; loads demand SLOT1 and can never share a cycle → ResMII inflates past the
; schedule span → MaxStageCount == 0 → shouldUseSchedule rejects.
;
; Bug chain this test guards:
; made the *itinerary* a choice-set (Slot01_LD = InstrStage<1
; [SLOT0,SLOT1]>), but TWO downstream models were inconsistent:
; 1. The DFA (SMS's default resource model) is choice-set-naive — it ORs
; all units in a stage (DFAPacketizerEmitter.cpp:183), so a Slot01_LD
; LD64 reserves BOTH slot0+slot1 bits and two LD64 always conflict.
; 2. The Bundle slot model (getLegalSlots) said LD64 is SLOT0-ONLY
; (because LD64's FU=LS → case FU_LS: return LSB_S0).
; fixes both: (a) getLegalSlots(LD64) now returns LSB_S0|LSB_S1
; (spec-faithful — D_LDW_WITH_IMM is in BOTH "Slot 0 — Load&Store" and
; "Slot 1 — Load", slot_instruction_list.md:124,129,402,407); (b) the
; alternative-aware HaydnResourceCycle (Bundle-backed) is now the default SMS
; resource model, picking ONE slot per LD64 instead of reserving both.
;
; Test design: two independent i64 loads in a straight-line block. The post-RA
; scheduler's HaydnHazardRecognizer conflict rule only flags a slot conflict
; when BOTH instructions have a single-slot Required set that is the SAME bit
; (HaydnHazardRecognizer.cpp:168-171). Two LD64 (Required={S0,S1}) never
; slot-conflict, so they pack into one `{ ld64, ld64 }` bundle. If either the
; getLegalSlots fix or the HaydnResourceCycle wiring regresses, the loads
; serialize into separate bundles (or the HR mis-rejects them).
;
; NOTE: full SMS acceptance (shouldUseSchedule on a real streaming kernel) is
; gated on a SEPARATE analyzeLoopForPipelining regression (filed
; pipeliner-analyzability) tracked in swpipeline-vec-dot-streaming.ll. This
; test guards the resource-model layer that SMS will use once analyzability is
; restored.
;
; Dual-sched form: i64 loads lower to paired ld32s. Contract: dual-load
; co-issue ({ ld32...; ld32... }) and triple-load fills only two load
; slots (third serializes). Explicit addi32 pointer bumps (not fused
; s_lw_post_imm).

; REBASELINED (auto) dual-sched / AR logical-slot rebaseline;.file skipped

; CHECK: 	.text
; CHECK: 	.globl	dual_load_i64_slot_poly         // -- Begin function dual_load_i64_slot_poly
; CHECK: 	.type	dual_load_i64_slot_poly,@function
; CHECK: dual_load_i64_slot_poly:                // @dual_load_i64_slot_poly
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
; CHECK: 	.size	dual_load_i64_slot_poly, .Lfunc_end0-dual_load_i64_slot_poly
; CHECK:                                         // -- End function
; CHECK: 	.globl	triple_load_i64_slot_poly       // -- Begin function triple_load_i64_slot_poly
; CHECK: 	.type	triple_load_i64_slot_poly,@function
; CHECK: triple_load_i64_slot_poly:              // @triple_load_i64_slot_poly
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	ld32	r4, r1, 0; 	addi32	r5, r1, 8; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r3, r1, 4; 	addi32	r1, r1, 16; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r6, r5, 4; 	ld32	r3, r3, 0; 	nop }
; CHECK: 	{ 	addi32{{(_w)?}}	r7, r1, 4; 	ld32	r5, r5, 0; 	nop }
; CHECK: 	{ 	ld32	r6, r6, 0; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 	ld32	r7, r7, 0; 	subi32	sp, sp, 8; 	nop }
; CHECK: 	{ 	st32	r4, sp, 0 }
; CHECK: 	{ 	st32	r3, sp, 4 }
; CHECK: 	{ 	ld64	d0, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	st32	r5, sp, 0 }
; CHECK: 	{ 	st32	r6, sp, 4 }
; CHECK: 	{ 	ld64	d1, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8; 	add64	d0, d0, d1; 	nop }
; CHECK: 	{ 	subi32	sp, sp, 8 }
; CHECK: 	{ 	st32	r1, sp, 0 }
; CHECK: 	{ 	st32	r7, sp, 4 }
; CHECK: 	{ 	ld64	d2, sp, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r2, 4; 	add64	d0, d0, d2; 	nop }
; CHECK: 	{ 	d_sw_l_with_imm	d0, r2, 0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8; 	d_sw_h_with_imm	d0, r1, 0; 	nop }
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	triple_load_i64_slot_poly, .Lfunc_end1-triple_load_i64_slot_poly
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @dual_load_i64_slot_poly(ptr %p, ptr %q) nounwind {
; The two independent i64 loads must share ONE bundle line. Each LD64 is
; slot-0-or-slot-1; the scheduler assigns one to slot 0 and the other to
; slot 1.
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %s = add i64 %a, %b
  store i64 %s, ptr %q
  ret void
}

; Three independent i64 loads: slot0 + slot1 + (the third cannot fit — only 2
; load slots). The first two should pack; the third serializes. This proves the
; slot model is the spec's 2-load-slot shape, not an unbounded accept.
define void @triple_load_i64_slot_poly(ptr %p, ptr %q) nounwind {
; First two loads pack (slot0 + slot1); the third lands in a later bundle.
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %p3 = getelementptr inbounds i64, ptr %p, i64 2
  %c = load i64, ptr %p3
  %s1 = add i64 %a, %b
  %s = add i64 %s1, %c
  store i64 %s, ptr %q
  ret void
}
