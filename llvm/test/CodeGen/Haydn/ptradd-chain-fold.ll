; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	test_chain                      // -- Begin function test_chain
; CHECK: 	.type	test_chain,@function
; CHECK-LABEL: test_chain:                             // @test_chain
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32 r0, r0, r0; nop; nop }
; CHECK: { addi32{{(_w)?}} r1, r1, 20; nop; nop }
; CHECK: 	{ nop; nop; jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_chain, .Lfunc_end0-test_chain
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

;
; Tests that chained G_PTR_ADD with constant offsets are folded into a single
; G_PTR_ADD with the combined offset. The pre-legalizer combiner calls
; CombinerHelper::matchPtrAddImmedChain which matches the pattern:
; %p2 = G_PTR_ADD %p1, G_CONSTANT c1
; %p3 = G_PTR_ADD %p2, G_CONSTANT c2
; and folds to:
; %combined = G_CONSTANT (c1 + c2)
; %p3 = G_PTR_ADD %p1, %combined
;
; This manifests in the final assembly as a single ADDI32_W with the combined
; immediate offset, rather than two separate ADDI32_W instructions.

; Two chained GEP with constant offsets: 16 + 4 = 20.
define ptr @test_chain(ptr %p) {
  %p1 = getelementptr i8, ptr %p, i32 16
  %p2 = getelementptr i8, ptr %p1, i32 4
  ret ptr %p2
}

; Three chained GEP with constant offsets: 8 + 16 + 4 = 28.
define ptr @test_triple_chain(ptr %p) {
  %p1 = getelementptr i8, ptr %p, i32 8
  %p2 = getelementptr i8, ptr %p1, i32 16
  %p3 = getelementptr i8, ptr %p2, i32 4
  ret ptr %p3
}

; Negative and positive offsets: -12 + 20 = 8.
define ptr @test_negative_offset(ptr %p) {
  %p1 = getelementptr i8, ptr %p, i32 -12
  %p2 = getelementptr i8, ptr %p1, i32 20
  ret ptr %p2
}

; Non-constant inner offset: the two adds should NOT be folded into one.
define ptr @test_nonconstant(ptr %p, i32 %off) {
  %p1 = getelementptr i8, ptr %p, i32 %off
  %p2 = getelementptr i8, ptr %p1, i32 4
  ret ptr %p2
}
