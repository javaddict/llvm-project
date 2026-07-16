; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 -O2 < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; NOTE: updated for VLIW slot-1 load promotion — independent loads now pack as ld32+ld32.
; NOTE: CHECKs reflect post- scheduled output (prologue/epilogue slot order varies per frame).
;
; REGRESSION TEST: Callee-saved register save/restore stress test.
;
; Purpose: Verify that callee-saved registers (R8-R11 for GPR, D8-D15 for
; DR64) are correctly saved in the prologue and restored in the epilogue.
; Also verify that the stride-4/stride-8 optimization for consecutive
; callee-save stores/loads works correctly.
;
; Haydn CSR list : R8-R11 (GPR), R15(LR), D8-D15 (DR64). R12 is the
; reserved linker/assembler scratch ("AT") and is NOT callee-saved.
;
; Why this test exists:
; The frame lowering has an optimized path for consecutive callee-saved
; registers: it sets R12 to SP + first_offset and uses stride-4 (GPR) or
; stride-8 (DR64) offsets. This optimization is delicate — historically it
; had to skip itself when R12 itself was in the callee-save list. Since
; R12 is never in the CSR list (it is reserved), so R12NeedsSaving is always
; false and the stride-4 path is always taken when >=2 GPR CSRs are saved.
;
; What these tests guard:
; 1. All callee-saved GPRs (R8-R11) are saved and restored when needed
; 2. All callee-saved DR64s (D8-D15) are saved and restored when needed
; 3. Prologue saves before epilogue restores (correct stack layout)
; 4. Stack allocation is correctly sized for all callee-save slots
; 5. The R12 scratch register optimization works (R12 is reserved AT, so the
; self-clobber case can no longer arise)
;
; If these tests fail, investigate HaydnFrameLowering::emitPrologue and
; emitEpilogue, specifically the R12NeedsSaving logic.
; Do NOT update CHECK lines without understanding the save/restore sequence.
;

; REBASELINED (auto) llc <stdin>;.file skipped

; CHECK:  	.text
; CHECK:  	.globl	test_minimal_callee_save        // -- Begin function test_minimal_callee_save
; CHECK:  	.type	test_minimal_callee_save,@function
; CHECK:  test_minimal_callee_save:               // @test_minimal_callee_save
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK:  	{ 	st32	lr, r2, 0 }
; CHECK:  	{ 	st32	r8, r2, 4 }
; CHECK:  	{ 	move32	r8, r1 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	add32	r1, r8, r1; 	nop }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 8 }
; CHECK:  	{ 	ld32	r8, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	test_minimal_callee_save, .Lfunc_end0-test_minimal_callee_save
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_all_gpr_callee_saved       // -- Begin function test_all_gpr_callee_saved
; CHECK:  	.type	test_all_gpr_callee_saved,@function
; CHECK:  test_all_gpr_callee_saved:              // @test_all_gpr_callee_saved
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 32 }
; CHECK:  	{ 	addi32{{(_w)?}}	r6, sp, 12 }
; CHECK:  	{ 	st32	lr, r6, 0 }
; CHECK:  	{ 	st32	r11, r6, 4 }
; CHECK:  	{ 	st32	r10, r6, 8 }
; CHECK:  	{ 	st32	r9, r6, 12 }
; CHECK:  	{ 	st32	r8, r6, 16 }
; CHECK:  	{ 	st32	r5, sp, 8; 	move32	r8, r2; 	move32	r9, r3 } // 4-byte Folded Spill
; CHECK:  	{ 	move32	r10, r4 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r11, r1; 	move32	r1, r8; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r8, r1; 	move32	r1, r9; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r9, r1; 	move32	r1, r10; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r10, r1; 	ld32	r1, sp, 8; 	xor32	r0, r0, r0 } // 4-byte Folded Reload
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	add32	r2, r11, r8; 	xor32	r0, r0, r0; 	nop }
; CHECK:  	{ 	add32	r2, r2, r9 }
; CHECK:  	{ 	add32	r2, r2, r10 }
; CHECK:  	{ 	add32	r1, r2, r1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	ld32	r11, sp, 16 }
; CHECK:  	{ 	ld32	r10, sp, 20 }
; CHECK:  	{ 	ld32	r9, sp, 24 }
; CHECK:  	{ 	ld32	r8, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	test_all_gpr_callee_saved, .Lfunc_end1-test_all_gpr_callee_saved
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_all_dr64_callee_saved      // -- Begin function test_all_dr64_callee_saved
; CHECK:  	.type	test_all_dr64_callee_saved,@function
; CHECK:  test_all_dr64_callee_saved:             // @test_all_dr64_callee_saved
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 72 }
; CHECK:  	{ 	xor32	r1, r1, r1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r1, 68 }
; CHECK:  	{ 	st32_reg	lr, sp, r1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, sp, 8 }
; CHECK:  	{ 	st64	d14, r1, 0 }
; CHECK:  	{ 	st64	d13, r1, 8 }
; CHECK:  	{ 	st64	d12, r1, 16 }
; CHECK:  	{ 	st64	d11, r1, 24 }
; CHECK:  	{ 	st64	d10, r1, 32 }
; CHECK:  	{ 	st64	d9, r1, 40 }
; CHECK:  	{ 	st64	d8, r1, 48 }
; CHECK:  	{ 	nop; 	or64	d9, d1, d1; 	or64	d10, d2, d2 }
; CHECK:  	{ 	or64	d11, d3, d3 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d8, d0, d0; 	or64	d0, d9, d9 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d9, d0, d0; 	or64	d0, d10, d10 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d10, d0, d0; 	or64	d0, d11, d11 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d11, d0, d0; 	or64	d0, d8, d8 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d12, d0, d0; 	or64	d0, d9, d9 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d13, d0, d0; 	or64	d0, d10, d10 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d14, d0, d0; 	or64	d0, d11, d11 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	add64	d1, d8, d9; 	nop }
; CHECK:  	{ 	add64	d1, d1, d10 }
; CHECK:  	{ 	add64	d1, d1, d11 }
; CHECK:  	{ 	add64	d1, d1, d12 }
; CHECK:  	{ 	add64	d1, d1, d13 }
; CHECK:  	{ 	add64	d1, d1, d14 }
; CHECK:  	{ 	add64	d0, d1, d0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld64	d14, sp, 8 }
; CHECK:  	{ 	ld64	d13, sp, 16 }
; CHECK:  	{ 	ld64	d12, sp, 24 }
; CHECK:  	{ 	ld64	d11, sp, 32 }
; CHECK:  	{ 	ld64	d10, sp, 40 }
; CHECK:  	{ 	ld64	d9, sp, 48 }
; CHECK:  	{ 	ld64	d8, sp, 56 }
; CHECK:  	{ 	ld32	lr, sp, 68 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 72 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end2:
; CHECK:  	.size	test_all_dr64_callee_saved, .Lfunc_end2-test_all_dr64_callee_saved
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_mixed_callee_saves         // -- Begin function test_mixed_callee_saves
; CHECK:  	.type	test_mixed_callee_saves,@function
; CHECK:  test_mixed_callee_saves:                // @test_mixed_callee_saves
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 48 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, sp, 28 }
; CHECK:  	{ 	st32	lr, r5, 0 }
; CHECK:  	{ 	st32	r11, r5, 4 }
; CHECK:  	{ 	st32	r10, r5, 8 }
; CHECK:  	{ 	st32	r9, r5, 12 }
; CHECK:  	{ 	st32	r8, r5, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, sp, 8 }
; CHECK:  	{ 	st64	d9, r5, 0 }
; CHECK:  	{ 	st64	d8, r5, 8 }
; CHECK:  	{ 	move32	r8, r2; 	move32	r9, r3; 	or64	d8, d0, d0 }
; CHECK:  	{ 	move32	r10, r4; 	or64	d9, d1, d1; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r11, r1; 	move32	r1, r8; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r8, r1; 	move32	r1, r9; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	move32	r9, r1; 	move32	r1, r10; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i32 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d0, d8, d8; 	move32	r10, r1 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	or64	d8, d0, d0; 	or64	d0, d9, d9 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, use_i64 }
; CHECK:  	{ 	add32	r1, r11, r8; 	add64	d0, d8, d0; 	xor32	r0, r0, r0 }
; CHECK:  	{ 	add32	r1, r1, r9 }
; CHECK:  	{ 	add32	r1, r1, r10 }
; CHECK:  	{ 	sext32t64	d1, r1 }
; CHECK:  	{ 	slli64	d1, d1, 32 }
; CHECK:  	{ 	srli64	d1, d1, 32 }
; CHECK:  	{ 	add64	d0, d0, d1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld64	d9, sp, 8 }
; CHECK:  	{ 	ld64	d8, sp, 16 }
; CHECK:  	{ 	ld32	lr, sp, 28 }
; CHECK:  	{ 	ld32	r11, sp, 32 }
; CHECK:  	{ 	ld32	r10, sp, 36 }
; CHECK:  	{ 	ld32	r9, sp, 40 }
; CHECK:  	{ 	ld32	r8, sp, 44 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 48 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end3:
; CHECK:  	.size	test_mixed_callee_saves, .Lfunc_end3-test_mixed_callee_saves
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_leaf_no_saves              // -- Begin function test_leaf_no_saves
; CHECK:  	.type	test_leaf_no_saves,@function
; CHECK:  test_leaf_no_saves:                     // @test_leaf_no_saves
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 42 }
; CHECK:  	{ 	add32	r1, r1, r2 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end4:
; CHECK:  	.size	test_leaf_no_saves, .Lfunc_end4-test_leaf_no_saves
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits


declare i32 @use_i32(i32)
declare i64 @use_i64(i64)

;Test 1: Minimal callee-save — just one value across a call.
;The allocator may use FP (R14) as a callee-saved register or R8-R11.
;Either way, a move32 copies the value to a callee-saved register before
;the call, and no explicit st32 is needed (only a register-to-register move).

define i32 @test_minimal_callee_save(i32 %x) nounwind {
entry:
; Value is moved to a callee-saved register (FP or R8-R12) before call
; After call, value is used from the callee-saved register
  %v1 = call i32 @use_i32(i32 %x)
  %result = add i32 %x, %v1
  ret i32 %result
}

;Test 2: All 4 callee-saved GPRs (R8-R11) used simultaneously.
;Forces the stride-4 optimization (R12 is reserved AT per, so it is
;always available as the scratch base register — R12NeedsSaving is always
;false). R12 is never among the saved registers.

define i32 @test_all_gpr_callee_saved(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) nounwind {
entry:
; Stack is allocated for callee-saves
; Multiple GPR callee-save stores (at least 3 of R8-R12)
; Epilogue: GPR callee-save restores
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %b)
  %v3 = call i32 @use_i32(i32 %c)
  %v4 = call i32 @use_i32(i32 %d)
  %v5 = call i32 @use_i32(i32 %e)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  ret i32 %s4
}

;Test 3: All callee-saved DR64 registers (D8-D15) used.
;Forces stride-8 optimization for DR64 saves/restores.

define i64 @test_all_dr64_callee_saved(i64 %a, i64 %b, i64 %c, i64 %d) nounwind {
entry:
; DR64 callee-saves use ST64 (8-byte stores)
; Epilogue: DR64 callee-save restores using LD64
  %v1 = call i64 @use_i64(i64 %a)
  %v2 = call i64 @use_i64(i64 %b)
  %v3 = call i64 @use_i64(i64 %c)
  %v4 = call i64 @use_i64(i64 %d)
  %v5 = call i64 @use_i64(i64 %v1)
  %v6 = call i64 @use_i64(i64 %v2)
  %v7 = call i64 @use_i64(i64 %v3)
  %v8 = call i64 @use_i64(i64 %v4)
  %s1 = add i64 %v1, %v2
  %s2 = add i64 %s1, %v3
  %s3 = add i64 %s2, %v4
  %s4 = add i64 %s3, %v5
  %s5 = add i64 %s4, %v6
  %s6 = add i64 %s5, %v7
  %s7 = add i64 %s6, %v8
  ret i64 %s7
}

;Test 4: Mixed GPR + DR64 callee-saves.
;Both R8-R12 and D8-D15 need saving, exercising both save sequences
;in the prologue and both restore sequences in the epilogue.

define i64 @test_mixed_callee_saves(i32 %a, i32 %b, i32 %c, i32 %d,
                                     i64 %e, i64 %f) nounwind {
entry:
; Prologue saves both GPR (st32) and DR64 (st64) callee-saves
; Epilogue restores both
  %v1 = call i32 @use_i32(i32 %a)
  %v2 = call i32 @use_i32(i32 %b)
  %v3 = call i32 @use_i32(i32 %c)
  %v4 = call i32 @use_i32(i32 %d)
  %v5 = call i64 @use_i64(i64 %e)
  %v6 = call i64 @use_i64(i64 %f)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %ext = zext i32 %s3 to i64
  %sd1 = add i64 %v5, %v6
  %result = add i64 %sd1, %ext
  ret i64 %result
}

;Test 5: Leaf function — no callee-saves needed at all.
;A simple leaf function should have no callee-save stores/loads.

define i32 @test_leaf_no_saves(i32 %x) nounwind {
entry:
; No callee-save stores
  %result = add i32 %x, 42
  ret i32 %result
}
