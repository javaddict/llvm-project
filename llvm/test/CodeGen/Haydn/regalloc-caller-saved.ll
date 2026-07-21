; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 -O2 < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; REGRESSION TEST: Caller-saved register clobber across calls.
;
; Purpose: Verify that caller-saved registers (R1-R7 for GPR, D0-D7 for DR64)
; are correctly spilled and reloaded around function calls. When a caller-saved
; register holds a live value that must survive across a call, the register
; allocator must either move it to a callee-saved register or spill it to the
; stack.
;
; Why this test exists:
; Caller-saved registers are clobbered by function calls. The register
; allocator must correctly track which values are live across calls and
; ensure they are not assigned to caller-saved registers if they need to
; survive. With only 4 callee-saved GPRs (R8-R11; R12 is reserved AT per
; and 8 callee-saved DR64s (D8-D15), the allocator must spill to
; stack when callee-saved registers are exhausted.
;
; What these tests guard:
; 1. Live values across calls are not lost when caller-saved registers are clobbered
; 2. The allocator prefers callee-saved registers for values live across calls
; 3. Stack spills happen when callee-saved registers are insufficient
; 4. Values are correctly reloaded after the call returns
; 5. Both GPR and DR64 caller-saved registers are handled
;
; If these tests fail, investigate the register allocator's spill logic and
; the call-preserved mask for the Haydn calling convention.
; Do NOT update CHECK lines without understanding the clobber pattern.
;

; REBASELINED (auto) llc <stdin>;.file skipped

; CHECK:  	.text
; CHECK:  	.globl	test_gpr_caller_saved_across_call // -- Begin function test_gpr_caller_saved_across_call
; CHECK:  	.type	test_gpr_caller_saved_across_call,@function
; CHECK:  test_gpr_caller_saved_across_call:      // @test_gpr_caller_saved_across_call
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 24 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, sp, 8 }
; CHECK:  	{ 	st32	lr, r4, 0 }
; CHECK:  	{ 	st32	r10, r4, 4 }
; CHECK:  	{ 	st32	r9, r4, 8 }
; CHECK:  	{ 	st32	r8, r4, 12 }
; CHECK:  	{ 	move32	r8, r1; 	move32	r9, r2; 	nop }
; CHECK:  	{ 	move32	r10, r3 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	add32	r2, r8, r9; 	xor32	r0, r0, r0; 	nop }
; CHECK:  	{ 	add32	r2, r2, r10 }
; CHECK:  	{ 	add32	r1, r2, r1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 8 }
; CHECK:  	{ 	ld32	r10, sp, 12 }
; CHECK:  	{ 	ld32	r9, sp, 16 }
; CHECK:  	{ 	ld32	r8, sp, 20 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	test_gpr_caller_saved_across_call, .Lfunc_end0-test_gpr_caller_saved_across_call
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_dr64_caller_saved_across_call // -- Begin function test_dr64_caller_saved_across_call
; CHECK:  	.type	test_dr64_caller_saved_across_call,@function
; CHECK:  test_dr64_caller_saved_across_call:     // @test_dr64_caller_saved_across_call
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 32 }
; CHECK:  	{ 	st32	lr, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, sp, 8 }
; CHECK:  	{ 	st64	d9, r1, 0 }
; CHECK:  	{ 	st64	d8, r1, 8 }
; CHECK:  	{ 	nop; 	or64	d8, d0, d0; 	or64	d9, d1, d1 }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_dr64 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	add64	d1, d8, d9; 	nop }
; CHECK:  	{ 	add64	d0, d1, d0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld64	d9, sp, 8 }
; CHECK:  	{ 	ld64	d8, sp, 16 }
; CHECK:  	{ 	ld32	lr, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	test_dr64_caller_saved_across_call, .Lfunc_end1-test_dr64_caller_saved_across_call
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_all_gpr_caller_saved       // -- Begin function test_all_gpr_caller_saved
; CHECK:  	.type	test_all_gpr_caller_saved,@function
; CHECK:  test_all_gpr_caller_saved:              // @test_all_gpr_caller_saved
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 40 }
; CHECK:  	{ 	addi32{{(_w)?}}	r12, sp, 20 }
; CHECK:  	{ 	st32	lr, r12, 0 }
; CHECK:  	{ 	st32	r11, r12, 4 }
; CHECK:  	{ 	st32	r10, r12, 8 }
; CHECK:  	{ 	st32	r9, r12, 12 }
; CHECK:  	{ 	st32	r8, r12, 16 }
; CHECK:  	{ 	st32	r1, sp, 16; 	move32	r9, r2; 	move32	r10, r3 } // 4-byte Folded Spill
; CHECK:  	{ 	st32	r6, sp, 12; 	move32	r11, r4; 	move32	r8, r5 } // 4-byte Folded Spill
; CHECK:  	{ 	st32	r7, sp, 8 }             // 4-byte Folded Spill
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	ld32	r2, sp, 16; 	ld32	r3, sp, 12; 	xor32	r0, r0, r0 } // 8-byte Folded Reload
; CHECK:  	{ 	add32	r2, r2, r9 }
; CHECK:  	{ 	add32	r2, r2, r10 }
; CHECK:  	{ 	add32	r2, r2, r11 }
; CHECK:  	{ 	add32	r2, r2, r8 }
; CHECK:  	{ 	add32	r2, r2, r3; 	ld32	r3, sp, 8; 	nop } // 4-byte Folded Reload
; CHECK:  	{ 	add32	r2, r2, r3 }
; CHECK:  	{ 	add32	r1, r2, r1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 20 }
; CHECK:  	{ 	ld32	r11, sp, 24 }
; CHECK:  	{ 	ld32	r10, sp, 28 }
; CHECK:  	{ 	ld32	r9, sp, 32 }
; CHECK:  	{ 	ld32	r8, sp, 36 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 40 }
; CHECK:  	{ 	nop }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end2:
; CHECK:  	.size	test_all_gpr_caller_saved, .Lfunc_end2-test_all_gpr_caller_saved
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_repeated_clobber           // -- Begin function test_repeated_clobber
; CHECK:  	.type	test_repeated_clobber,@function
; CHECK:  test_repeated_clobber:                  // @test_repeated_clobber
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 32 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 12 }
; CHECK:  	{ 	st32	lr, r2, 0 }
; CHECK:  	{ 	st32	r11, r2, 4 }
; CHECK:  	{ 	st32	r10, r2, 8 }
; CHECK:  	{ 	st32	r9, r2, 12 }
; CHECK:  	{ 	st32	r8, r2, 16 }
; CHECK:  	{ 	st32	r1, sp, 8 }             // 4-byte Folded Spill
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	xor32	r0, r0, r0; 	move32	r9, r1; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	xor32	r0, r0, r0; 	move32	r10, r1; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	xor32	r0, r0, r0; 	move32	r11, r1; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	xor32	r0, r0, r0; 	move32	r8, r1; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_gpr }
; CHECK:  	{ 	ld32	r2, sp, 8; 	xor32	r0, r0, r0; 	nop } // 4-byte Folded Reload
; CHECK:  	{ 	add32	r2, r2, r9 }
; CHECK:  	{ 	add32	r2, r2, r10 }
; CHECK:  	{ 	add32	r2, r2, r11 }
; CHECK:  	{ 	add32	r2, r2, r8 }
; CHECK:  	{ 	add32	r1, r2, r1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	ld32	r11, sp, 16 }
; CHECK:  	{ 	ld32	r10, sp, 20 }
; CHECK:  	{ 	ld32	r9, sp, 24 }
; CHECK:  	{ 	ld32	r8, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK:  	{ 	nop }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end3:
; CHECK:  	.size	test_repeated_clobber, .Lfunc_end3-test_repeated_clobber
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_both_banks_clobbered       // -- Begin function test_both_banks_clobbered
; CHECK:  	.type	test_both_banks_clobbered,@function
; CHECK:  test_both_banks_clobbered:              // @test_both_banks_clobbered
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 24 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 16 }
; CHECK:  	{ 	st32	lr, r2, 0 }
; CHECK:  	{ 	st32	r8, r2, 4 }
; CHECK:  	{ 	st64	d8, sp, 8 }
; CHECK:  	{ 	move32	r8, r1; 	or64	d8, d0, d0; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, clobber_all }
; CHECK:  	{ 	xor32	r0, r0, r0; 	sext32t64	d0, r8; 	nop }
; CHECK:  	{ 	slli64	d0, d0, 32 }
; CHECK:  	{ 	srli64	d0, d0, 32 }
; CHECK:  	{ 	add64	d0, d8, d0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld64	d8, sp, 8 }
; CHECK:  	{ 	ld32	lr, sp, 16 }
; CHECK:  	{ 	ld32	r8, sp, 20 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end4:
; CHECK:  	.size	test_both_banks_clobbered, .Lfunc_end4-test_both_banks_clobbered
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits


declare i32 @clobber_gpr()
declare i64 @clobber_dr64()
declare void @clobber_all()

;Test 1: GPR caller-saved registers live across call.
;R1-R7 are caller-saved. Values in R1-R7 at the call site must be saved.

define i32 @test_gpr_caller_saved_across_call(i32 %a, i32 %b, i32 %c) nounwind {
entry:
; %a, %b, %c arrive in R1, R2, R3 (caller-saved). All three must survive
; the call to @clobber_gpr. The allocator should move them to callee-saved
; registers or spill to stack.
  %call_result = call i32 @clobber_gpr()
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %call_result
  ret i32 %s3
}

;Test 2: DR64 caller-saved registers live across call.
;D0-D7 are caller-saved. Values in D0-D7 at the call site must be saved.

define i64 @test_dr64_caller_saved_across_call(i64 %a, i64 %b) nounwind {
entry:
; %a, %b arrive in D0, D1 (caller-saved). Both must survive the call.
  %call_result = call i64 @clobber_dr64()
  %s1 = add i64 %a, %b
  %s2 = add i64 %s1, %call_result
  ret i64 %s2
}

;Test 3: All GPR caller-saved registers (R1-R7) holding live values.
;7 live i32 values across a call forces heavy spill or callee-saved usage.

define i32 @test_all_gpr_caller_saved(i32 %a, i32 %b, i32 %c, i32 %d,
                                       i32 %e, i32 %f, i32 %g) nounwind {
entry:
; All 7 args arrive in R1-R7. The call clobbers all of them.
; At least 7 values must survive (some in callee-saved, some spilled).
; After call, values must be reloaded from stack or callee-saved registers
  %call_result = call i32 @clobber_gpr()
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g
  %s7 = add i32 %s6, %call_result
  ret i32 %s7
}

;Test 4: Multiple calls clobbering caller-saved registers repeatedly.
;Values must survive across multiple call boundaries.

define i32 @test_repeated_clobber(i32 %x) nounwind {
entry:
; %x must survive across all 5 calls
  %r1 = call i32 @clobber_gpr()
  %r2 = call i32 @clobber_gpr()
  %r3 = call i32 @clobber_gpr()
  %r4 = call i32 @clobber_gpr()
  %r5 = call i32 @clobber_gpr()
  %s1 = add i32 %x, %r1
  %s2 = add i32 %s1, %r2
  %s3 = add i32 %s2, %r3
  %s4 = add i32 %s3, %r4
  %s5 = add i32 %s4, %r5
  ret i32 %s5
}

;Test 5: Both GPR and DR64 caller-saved registers clobbered by one call.
;void @clobber_all clobbers all caller-saved registers in both banks.

define i64 @test_both_banks_clobbered(i32 %a, i64 %b) nounwind {
entry:
; %a (GPR R1) and %b (DR64 D0) are both caller-saved and must survive.
  call void @clobber_all()
  %ext_a = zext i32 %a to i64
  %result = add i64 %b, %ext_a
  ret i64 %result
}
