; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s



;
; Tests for bit manipulation simplification combines in HaydnPostLegalizerCombiner.
; Each function exercises a specific bit-simplify pattern. The post-legalizer
; combiner runs after legalization but before RegBankSelect, so all combines
; operate on generic MIR.
;
; Combiner rules tested here:
; xor_xor_constant_fold: (A ^ C1) ^ C2 -> A ^ (C1^C2)
; double_not: XOR(NOT(x)) -> x
; and_or_disjoint: (A & MaskC) | SetC where disjoint, MaskC|SetC=all-ones -> A | SetC
; shift_mask_redundant: (x >> C) & exact_remaining_mask -> x >> C
; xor_zero: G_XOR x, 0 -> x

;===--- xor_xor_constant_fold: (A ^ 0x0F) ^ 0xF0 -> A ^ 0xFF ---===

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	xor_xor_const_fold              // -- Begin function xor_xor_const_fold
; CHECK: 	.type	xor_xor_const_fold,@function
; CHECK: xor_xor_const_fold:                     // @xor_xor_const_fold
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ xori32	r1, r1, 255; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	xor_xor_const_fold, .Lfunc_end0-xor_xor_const_fold
; CHECK:                                         // -- End function
; CHECK: 	.globl	xor_xor_cancel                  // -- Begin function xor_xor_cancel
; CHECK: 	.type	xor_xor_cancel,@function
; CHECK: xor_xor_cancel:                         // @xor_xor_cancel
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	xor_xor_cancel, .Lfunc_end1-xor_xor_cancel
; CHECK:                                         // -- End function
; CHECK: 	.globl	double_not                      // -- Begin function double_not
; CHECK: 	.type	double_not,@function
; CHECK: double_not:                             // @double_not
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	double_not, .Lfunc_end2-double_not
; CHECK:                                         // -- End function
; CHECK: 	.globl	and_or_disjoint_not_full        // -- Begin function and_or_disjoint_not_full
; CHECK: 	.type	and_or_disjoint_not_full,@function
; CHECK: and_or_disjoint_not_full:               // @and_or_disjoint_not_full
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ andi32	r1, r1, 255; nop; nop }
; CHECK: 	{ ori32	r1, r1, 65280; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	and_or_disjoint_not_full, .Lfunc_end3-and_or_disjoint_not_full
; CHECK:                                         // -- End function
; CHECK: 	.globl	and_or_disjoint_full            // -- Begin function and_or_disjoint_full
; CHECK: 	.type	and_or_disjoint_full,@function
; CHECK: and_or_disjoint_full:                   // @and_or_disjoint_full
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ nop; nop; lui	r2, 4080 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, 65280 }
; CHECK: 	{ or32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	and_or_disjoint_full, .Lfunc_end4-and_or_disjoint_full
; CHECK:                                         // -- End function
; CHECK: 	.globl	shift_mask_redundant_lshr       // -- Begin function shift_mask_redundant_lshr
; CHECK: 	.type	shift_mask_redundant_lshr,@function
; CHECK: shift_mask_redundant_lshr:              // @shift_mask_redundant_lshr
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ srli32	r1, r1, 8; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	shift_mask_redundant_lshr, .Lfunc_end5-shift_mask_redundant_lshr
; CHECK:                                         // -- End function
; CHECK: 	.globl	shift_mask_not_redundant        // -- Begin function shift_mask_not_redundant
; CHECK: 	.type	shift_mask_not_redundant,@function
; CHECK: shift_mask_not_redundant:               // @shift_mask_not_redundant
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ srli32	r1, r1, 8; nop; nop }
; CHECK: 	{ andi32	r1, r1, 255; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	shift_mask_not_redundant, .Lfunc_end6-shift_mask_not_redundant
; CHECK:                                         // -- End function
; CHECK: 	.globl	xor_zero_identity               // -- Begin function xor_zero_identity
; CHECK: 	.type	xor_zero_identity,@function
; CHECK: xor_zero_identity:                      // @xor_zero_identity
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	xor_zero_identity, .Lfunc_end7-xor_zero_identity
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @xor_xor_const_fold(i32 %a) nounwind {
; The two XOR constants should be folded into one.
  %t1 = xor i32 %a, 15
  %r = xor i32 %t1, 240
  ret i32 %r
}

;===--- xor_xor_constant_fold cancellation: (A ^ C) ^ C -> A ---===

define i32 @xor_xor_cancel(i32 %a) nounwind {
; XOR with same constant twice cancels out -> identity (no xor at all).
  %t1 = xor i32 %a, 42
  %r = xor i32 %t1, 42
  ret i32 %r
}

;===--- double_not: XOR(XOR(x, -1), -1) -> x ---===

define i32 @double_not(i32 %a) nounwind {
; Two NOT operations cancel out -> identity.
  %not1 = xor i32 %a, -1
  %r = xor i32 %not1, -1
  ret i32 %r
}

;===--- and_or_disjoint: (A & 0x00FF) | 0xFF00 -> A | 0xFF00 ---===
; MaskC=0x00FF and SetC=0xFF00 are disjoint, MaskC|SetC=0xFFFF != all-ones.
; This should NOT simplify (not all-ones coverage).

define i32 @and_or_disjoint_not_full(i32 %a) nounwind {
; The mask does NOT cover all bits, so no simplification.
  %masked = and i32 %a, 255
  %r = or i32 %masked, 65280
  ret i32 %r
}

;===--- and_or_disjoint: (A & 0x00FF00FF) | 0xFF00FF00 -> A | 0xFF00FF00 ---===
; MaskC=0x00FF00FF and SetC=0xFF00FF00 are disjoint, MaskC|SetC=0xFFFFFFFF = all-ones.
; This SHOULD simplify to (A | SetC).

define i32 @and_or_disjoint_full(i32 %a) nounwind {
; MaskC|SetC covers all 32 bits -> simplifies to A | SetC.
  %masked = and i32 %a, 16711935
  %r = or i32 %masked, 4278255360
  ret i32 %r
}

;===--- shift_mask_redundant: (x >>u 8) & 0x00FFFFFF -> x >>u 8 ---===

define i32 @shift_mask_redundant_lshr(i32 %a) nounwind {
; After lshr by 8, only the low 24 bits are meaningful. Mask 0x00FFFFFF
; covers exactly those bits -> the AND is redundant.
  %shifted = lshr i32 %a, 8
  %r = and i32 %shifted, 16777215
  ret i32 %r
}

;===--- shift_mask NOT redundant: (x >>u 8) & 0xFF -> no simplify ---===

define i32 @shift_mask_not_redundant(i32 %a) nounwind {
; Mask 0xFF only covers 8 of the 24 meaningful bits -> AND is NOT redundant.
  %shifted = lshr i32 %a, 8
  %r = and i32 %shifted, 255
  ret i32 %r
}

;===--- xor_zero: G_XOR x, 0 -> x ---===
; Note: This is also covered by right_identity_zero in TableGen rules.
; Included here for completeness of the bit-simplify test suite.

define i32 @xor_zero_identity(i32 %x) nounwind {
  %r = xor i32 %x, 0
  ret i32 %r
}
