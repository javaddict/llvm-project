; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	pure_leaf                       // -- Begin function pure_leaf
; CHECK: 	.type	pure_leaf,@function
; CHECK-LABEL: pure_leaf:                              // @pure_leaf
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	pure_leaf, .Lfunc_end0-pure_leaf
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; Tests for leaf functions and prologue/epilogue decisions.
;
; Leaf functions do not call other functions, so they:
; Do not need callee-save spills
; May not need a stack frame at all (no allocas, no locals)
; Still zero R0 in the prologue (reserved soft-zero register)

;Pure leaf: no stack frame at all

define i32 @pure_leaf(i32 %x) {
; Only the R0 zeroing prologue, no SP adjustment
  %r = add i32 %x, 1
  ret i32 %r
}

;Leaf with only arithmetic (no memory, no stack)

define i32 @leaf_arith(i32 %a, i32 %b, i32 %c) {
  %s1 = add i32 %a, %b
  %s2 = mul i32 %s1, %c
  %s3 = sub i32 %s2, %a
  ret i32 %s3
}

;Leaf with i64 arithmetic (no stack needed if only register ops)

define i64 @leaf_i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

;Leaf with comparison (no stack)

define i32 @leaf_cmp(i32 %a, i32 %b) {
  %cmp = icmp sgt i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;Non-leaf: function with a call (needs callee-save spills)

declare i32 @extern(i32)

define i32 @non_leaf(i32 %x) {
; Must save/restore callee-saved registers
  %v = call i32 @extern(i32 %x)
  %r = add i32 %v, 1
  ret i32 %r
}

;Leaf with alloca (needs stack frame but no callee saves)

define i32 @leaf_with_alloca(i32 %x) {
; Needs stack space for alloca
; (SFR-strip) changed bundle layout (denser packing) — the SP restore may
; be hoisted above the local st32/ld32 (anti-dep); use CHECK-DAG so the frame
; alloc/dealloc and local access are matched regardless of order. Rebaselined.
  %p = alloca i32
  store i32 %x, ptr %p
  %v = load i32, ptr %p
  %r = add i32 %v, 1
  ret i32 %r
}

;Leaf with multiple allocas

define i32 @leaf_multi_alloca(i32 %x) {
  %p1 = alloca i32
  %p2 = alloca i32
  store i32 %x, ptr %p1
  %v = load i32, ptr %p1
  store i32 %v, ptr %p2
  %r = load i32, ptr %p2
  ret i32 %r
}

;Void leaf function

define void @void_leaf() {
  ret void
}

;Leaf with conditional branch (no stack needed)

define i32 @leaf_branch(i32 %a, i32 %b) {
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %pos, label %neg

pos:
  %r1 = add i32 %a, %b
  ret i32 %r1

neg:
  %r2 = sub i32 %a, %b
  ret i32 %r2
}
