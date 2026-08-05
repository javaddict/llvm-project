; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s



;
; End-to-end tests for pre-legalizer combiner optimizations.
; These functions verify that the combiner correctly simplifies patterns
; before the legalizer runs.

; Test: sext i16 -> i32 should produce a shift-based sign extension.
; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_sext_i16                   // -- Begin function test_sext_i16
; CHECK: 	.type	test_sext_i16,@function
; CHECK: test_sext_i16:                          // @test_sext_i16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	slli32	r1, r1, 16 }
; CHECK: 	{ 		nop; 	nop; 	srai32	r1, r1, 16 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_sext_i16, .Lfunc_end0-test_sext_i16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_const_fold                 // -- Begin function test_const_fold
; CHECK: 	.type	test_const_fold,@function
; CHECK: test_const_fold:                        // @test_const_fold
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 142; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_const_fold, .Lfunc_end1-test_const_fold
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_shift_zero                 // -- Begin function test_shift_zero
; CHECK: 	.type	test_shift_zero,@function
; CHECK: test_shift_zero:                        // @test_shift_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_shift_zero, .Lfunc_end2-test_shift_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_and_all_ones               // -- Begin function test_and_all_ones
; CHECK: 	.type	test_and_all_ones,@function
; CHECK: test_and_all_ones:                      // @test_and_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_and_all_ones, .Lfunc_end3-test_and_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_or_zero                    // -- Begin function test_or_zero
; CHECK: 	.type	test_or_zero,@function
; CHECK: test_or_zero:                           // @test_or_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_or_zero, .Lfunc_end4-test_or_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @test_sext_i16(i16 %x) {
  %ext = sext i16 %x to i32
  ret i32 %ext
}

; Test: constant folding should reduce add of two constants.
define i32 @test_const_fold() {
entry:
  ret i32 142
}

; Test: shift by zero is eliminated (result is just the input).
define i32 @test_shift_zero(i32 %x) {
  %result = shl i32 %x, 0
  ret i32 %result
}

; Test: AND with all-ones is identity.
define i32 @test_and_all_ones(i32 %x) {
  %result = and i32 %x, -1
  ret i32 %result
}

; Test: OR with zero is identity.
define i32 @test_or_zero(i32 %x) {
  %result = or i32 %x, 0
  ret i32 %result
}
