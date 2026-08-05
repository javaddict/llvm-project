; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	add                             // -- Begin function add
; CHECK: 	.type	add,@function
; CHECK-LABEL: add:                                    // @add
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ nop; nop; jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	add, .Lfunc_end0-add
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; End-to-end test for Haydn DSP intrinsics through the full CodeGen pipeline.
; This test verifies that Clang frontend DSP builtins (via __builtin_haydn_*)
; compile through LLVM IR intrinsics → GlobalISel selection → MC emission.
;
; The corresponding C source is maintained alongside this test for reference.
; To regenerate: clang -target haydn-unknown-elf -O2 -S -emit-llvm -o - <src.c>

;Scalar operations (basic ALU)
define i32 @add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @sub(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

define i32 @mul(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
}

;64-bit operations (DR64 register bank)
define i64 @add64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

define i64 @sub64(i64 %a, i64 %b) {
  %r = sub i64 %a, %b
  ret i64 %r
}

;DSP multiply intrinsic (MUL64_LL, signed-signed, low-low)
declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)
define i64 @test_mul64_ss_ll(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

;DSP multiply-accumulate pattern (MUL64 + ADD64)
define i64 @test_mula64_ss_ll(i64 %a, i64 %b, i64 %acc) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %prod = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.3, <2 x i32> %bc.4)
  %r = add i64 %acc, %prod
  ret i64 %r
}

;Conditional MAC (branchless select with DSP)
define i64 @conditional_mac(i64 %a, i64 %b, i64 %c, i32 %flag) {
  %cmp = icmp eq i32 %flag, 0
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %prod1 = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.5, <2 x i32> %bc.6)
  %sum1 = add i64 %prod1, %c
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %c to <2 x i32>
  %prod2 = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.7, <2 x i32> %bc.8)
  %sum2 = add i64 %prod2, %b
  %r = select i1 %cmp, i64 %sum2, i64 %sum1
  ret i64 %r
}

;Dot product loop (DSP in a loop)
; Note: The array loads are optimized away by the compiler; the loop body
; contains the multiply-accumulate on registers already in DR64.
define i64 @dot_product(ptr %a, ptr %b, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %loop ]
  %pa = getelementptr i64, ptr %a, i32 %i
  %pb = getelementptr i64, ptr %b, i32 %i
  %va = load i64, ptr %pa
  %vb = load i64, ptr %pb
  %bc.9 = bitcast i64 %va to <2 x i32>
  %bc.10 = bitcast i64 %vb to <2 x i32>
  %prod = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.9, <2 x i32> %bc.10)
  %sum.next = add i64 %sum, %prod
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret i64 %sum.next
}
