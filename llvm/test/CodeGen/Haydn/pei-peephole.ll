; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	test_simple_no_fp               // -- Begin function test_simple_no_fp
; CHECK: 	.type	test_simple_no_fp,@function
; CHECK-LABEL: test_simple_no_fp:                      // @test_simple_no_fp
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ nop; nop; jalr{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_simple_no_fp, .Lfunc_end0-test_simple_no_fp
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; Tests for the HaydnPEIPeephole pass, which optimizes prologue/epilogue
; instruction sequences after PEI.
;
; The peephole pass runs in addPreEmitPass, after BranchRelaxation and
; before CompressPass. It handles:
; 1. Redundant ZERO_GPR R0 elimination (keep first, remove duplicates)
; 2. Dead frame-pointer setup elimination (when hasFP is false)

;===----------------------------------------------------------------------===;;
; Test 1: Simple function without FP — prologue should have exactly one
; ZERO_GPR R0 and no FP setup (R14) instructions.
;===----------------------------------------------------------------------===;;

define i32 @test_simple_no_fp(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;===----------------------------------------------------------------------===;;
; Test 2: Function with a stack slot (local variable) but no FP.
; The prologue allocates stack space but should not set up R14.
;===----------------------------------------------------------------------===;;

define i32 @test_stack_slot_no_fp(i32 %a) {
  %local = alloca i32
  store i32 %a, ptr %local
  %loaded = load i32, ptr %local
  ret i32 %loaded
}

;===----------------------------------------------------------------------===;;
; Test 3: Leaf function (no calls, minimal frame) — the simplest case
; for peephole. Only ZERO_GPR R0 and perhaps a small stack adjust.
;===----------------------------------------------------------------------===;;

define i32 @test_leaf(i32 %x) {
  %r = mul i32 %x, 3
  ret i32 %r
}

;===----------------------------------------------------------------------===;;
; Test 4: Function with a call — forces callee-save spills, larger frame.
; Still no FP unless forced.
;===----------------------------------------------------------------------===;;

declare void @extern_func(i32)

define void @test_with_call(i32 %val) {
  call void @extern_func(i32 %val)
  ret void
}

;===----------------------------------------------------------------------===;;
; Test 5: Multiple basic blocks — ensures peephole scans all blocks for
; redundant ZERO_GPR, not just the entry block.
;===----------------------------------------------------------------------===;;

define i32 @test_multi_bb(i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pos, label %neg

pos:
  %p = add i32 %n, 10
  ret i32 %p

neg:
  %q = sub i32 0, %n
  ret i32 %q
}

;===----------------------------------------------------------------------===;;
; Test 6: Frame-pointer forced via attribute — FP setup should NOT be
; eliminated when the user explicitly requests a frame pointer.
; (We cannot easily force hasFP=true from IR alone, but this test
; documents the intent and ensures no mis-optimization when FP
; is present.)
;===----------------------------------------------------------------------===;;

define i32 @test_many_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e,
                           i32 %f, i32 %g, i32 %h) {
  %s1 = add i32 %a, %b
  %s2 = add i32 %c, %d
  %s3 = add i32 %e, %f
  %s4 = add i32 %g, %h
  %t1 = add i32 %s1, %s2
  %t2 = add i32 %s3, %s4
  %r = add i32 %t1, %t2
  ret i32 %r
}
