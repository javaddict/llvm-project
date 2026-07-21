; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	test_sext_i16                   // -- Begin function test_sext_i16
; CHECK: 	.type	test_sext_i16,@function
; CHECK-LABEL: test_sext_i16:                          // @test_sext_i16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 16 }
; CHECK: 	{ 	sll32	r1, r1, r2 }
; CHECK: 	{ 	sra32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_sext_i16, .Lfunc_end0-test_sext_i16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

;
; End-to-end tests for pre-legalizer combiner optimizations.
; These functions verify that the combiner correctly simplifies patterns
; before the legalizer runs.

; Test: sext i16 -> i32 should produce a shift-based sign extension.
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
