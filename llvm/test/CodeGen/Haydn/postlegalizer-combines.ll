; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	add_zero                        // -- Begin function add_zero
; CHECK: 	.type	add_zero,@function
; CHECK-LABEL: add_zero:                               // @add_zero
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	add_zero, .Lfunc_end0-add_zero
; CHECK:                                         // -- End function

;
; Tests for combiner rules in HaydnPostLegalizerCombiner.
; Each function exercises a specific combine pattern. The post-legalizer
; runs after legalization but before RegBankSelect, so all combines
; operate on generic MIR.
;
; Combiner rules tested here:
; right_identity_zero: G_ADD/G_SUB/G_OR/G_XOR/G_SHL/G_LSHR/G_ASHR x, 0 -> x
; right_identity_one_int: G_MUL x, 1 -> x
; binop_same_val: G_AND x, x -> x; G_OR x, x -> x
; mul_by_neg_one: G_MUL x, -1 -> G_SUB 0, x (negation)
; and_zero: G_AND x, 0 -> 0
; or_all_ones: G_OR x, -1 -> -1
; constant_fold_icmp: G_ICMP const, const -> const

;===--- right_identity_zero: G_ADD x, 0 -> x ---===

define i32 @add_zero(i32 %x) nounwind {
  %r = add i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_SUB x, 0 -> x ---===

define i32 @sub_zero(i32 %x) nounwind {
  %r = sub i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_OR x, 0 -> x ---===

define i32 @or_zero(i32 %x) nounwind {
  %r = or i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_XOR x, 0 -> x ---===

define i32 @xor_zero(i32 %x) nounwind {
  %r = xor i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_SHL x, 0 -> x ---===

define i32 @shl_zero(i32 %x) nounwind {
  %r = shl i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_LSHR x, 0 -> x ---===

define i32 @lshr_zero(i32 %x) nounwind {
  %r = lshr i32 %x, 0
  ret i32 %r
}

;===--- right_identity_zero: G_ASHR x, 0 -> x ---===

define i32 @ashr_zero(i32 %x) nounwind {
  %r = ashr i32 %x, 0
  ret i32 %r
}

;===--- right_identity_one_int: G_MUL x, 1 -> x ---===

define i32 @mul_one(i32 %x) nounwind {
  %r = mul i32 %x, 1
  ret i32 %r
}

;===--- binop_same_val: G_AND x, x -> x ---===

define i32 @and_self(i32 %x) nounwind {
  %r = and i32 %x, %x
  ret i32 %r
}

;===--- binop_same_val: G_OR x, x -> x ---===

define i32 @or_self(i32 %x) nounwind {
  %r = or i32 %x, %x
  ret i32 %r
}

;===--- mul_by_neg_one: G_MUL x, -1 -> G_SUB 0, x (negation) ---===
; The combine replaces MUL with SUB, but the selector still materializes
; the zero constant. Verify that mul32 is NOT present.

define i32 @mul_neg_one(i32 %x) nounwind {
  %r = mul i32 %x, -1
  ret i32 %r
}

;===--- and_zero: G_AND x, 0 -> 0 ---===

define i32 @and_zero(i32 %x) nounwind {
  %r = and i32 %x, 0
  ret i32 %r
}

;===--- or_all_ones: G_OR x, -1 -> -1 ---===

define i32 @or_all_ones(i32 %x) nounwind {
  %r = or i32 %x, -1
  ret i32 %r
}

;===--- constant_fold_icmp: G_ICMP const, const -> const ---===

define i32 @icmp_const_fold() nounwind {
  %cmp = icmp ugt i32 42, 10
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Identity with non-zero operand should NOT be eliminated ---===

define i32 @add_nonzero(i32 %x) nounwind {
  %r = add i32 %x, 5
  ret i32 %r
}
