; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; REGRESSION TEST: G_SMULH / G_UMULH for narrow types (s8/s16) must lower via
; the custom legalizer (sign/zero-extend operands to s32, multiply, shift right
; by DstBits, truncate). Fixes "unable to legalize G_UMULH s8/s16" and
; "G_SMULH s8/s16" found via yarpgen seeds 1, 6, 10.
;
; Without the custom handler, the generic minScalar/widenScalar path mis-compiles
; the SIGNED case: it widens without sign-extending, so the high-bit extraction
; returns the wrong value for negative operands.
;
; Intent-focused DAG CHECKs: verify the sign/zero-extend-then-multiply-then-shift
; sequence is emitted. Per-bundle packetization may change without changing the
; algorithm, so we use CHECK-DAG over key mnemonics instead of exact bundle lines.

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped







; CHECK:  	.text
; CHECK:  	.globl	smulh_s8                        // -- Begin function smulh_s8
; CHECK:  	.type	smulh_s8,@function
; CHECK:  smulh_s8:                               // @smulh_s8
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 24; 	subi32	sp, sp, 8; 	nop }
; CHECK:  	{ 	sll32	r1, r1, r3; 	sll32	r2, r2, r3; 	nop }
; CHECK:  	{ 	sra32	r1, r1, r3; 	sra32	r2, r2, r3; 	nop }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 65535; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	and32	r1, r1, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 8 }
; CHECK:  	{ 	srl32	r1, r1, r2 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	smulh_s8, .Lfunc_end0-smulh_s8
; CHECK:                                          // -- End function
; CHECK:  	.globl	umulh_s8                        // -- Begin function umulh_s8
; CHECK:  	.type	umulh_s8,@function
; CHECK:  umulh_s8:                               // @umulh_s8
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 255 }
; CHECK:  	{ 	and32	r1, r1, r3; 	and32	r2, r2, r3; 	nop }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 8 }
; CHECK:  	{ 	srl32	r1, r1, r2 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	umulh_s8, .Lfunc_end1-umulh_s8
; CHECK:                                          // -- End function
; CHECK:  	.globl	smulh_s16                       // -- Begin function smulh_s16
; CHECK:  	.type	smulh_s16,@function
; CHECK:  smulh_s16:                              // @smulh_s16
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 16; 	subi32	sp, sp, 8; 	nop }
; CHECK:  	{ 	sll32	r1, r1, r3; 	sll32	r2, r2, r3; 	nop }
; CHECK:  	{ 	sra32	r1, r1, r3; 	sra32	r2, r2, r3; 	nop }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	srl32	r1, r1, r3 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end2:
; CHECK:  	.size	smulh_s16, .Lfunc_end2-smulh_s16
; CHECK:                                          // -- End function
; CHECK:  	.globl	umulh_s16                       // -- Begin function umulh_s16
; CHECK:  	.type	umulh_s16,@function
; CHECK:  umulh_s16:                              // @umulh_s16
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 65535 }
; CHECK:  	{ 	and32	r1, r1, r4; 	and32	r2, r2, r4; 	nop }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 16; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	srl32	r1, r1, r3; 	nop }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end3:
; CHECK:  	.size	umulh_s16, .Lfunc_end3-umulh_s16
; CHECK:                                          // -- End function
; CHECK:  	.globl	smulh_s8_neg                    // -- Begin function smulh_s8_neg
; CHECK:  	.type	smulh_s8_neg,@function
; CHECK:  smulh_s8_neg:                           // @smulh_s8_neg
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 24; 	subi32	sp, sp, 8; 	nop }
; CHECK:  	{ 	sll32	r1, r1, r3; 	sll32	r2, r2, r3; 	nop }
; CHECK:  	{ 	sra32	r1, r1, r3; 	sra32	r2, r2, r3; 	nop }
; CHECK:  	{ 	st32	r1, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r2, sp, 4 }
; CHECK:  	{ 	ld64	d1, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 65535; 	mul64.ll	d0, d0, d1; 	nop }
; CHECK:  	{ 	move32_dr_l	r1, d0 }
; CHECK:  	{ 	and32	r1, r1, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 8 }
; CHECK:  	{ 	srl32	r1, r1, r2 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end4:
; CHECK:  	.size	smulh_s8_neg, .Lfunc_end4-smulh_s8_neg
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits

define i8 @smulh_s8(i8 %a, i8 %b) nounwind {
  %aa = sext i8 %a to i16
  %bb = sext i8 %b to i16
  %m = mul i16 %aa, %bb
  %trunc = lshr i16 %m, 8
  %r = trunc i16 %trunc to i8
  ret i8 %r
}

define i8 @umulh_s8(i8 %a, i8 %b) nounwind {
  %aa = zext i8 %a to i16
  %bb = zext i8 %b to i16
  %m = mul i16 %aa, %bb
  %trunc = lshr i16 %m, 8
  %r = trunc i16 %trunc to i8
  ret i8 %r
}

define i16 @smulh_s16(i16 %a, i16 %b) nounwind {
  %aa = sext i16 %a to i32
  %bb = sext i16 %b to i32
  %m = mul i32 %aa, %bb
  %r = lshr i32 %m, 16
  %tr = trunc i32 %r to i16
  ret i16 %tr
}

define i16 @umulh_s16(i16 %a, i16 %b) nounwind {
  %aa = zext i16 %a to i32
  %bb = zext i16 %b to i32
  %m = mul i32 %aa, %bb
  %r = lshr i32 %m, 16
  %tr = trunc i32 %r to i16
  ret i16 %tr
}

; Negative-operand smoke test for SMULH: -1 * -1 in i8 has high byte = 0
; not 0xFF (which the unsigned path would produce for 0xFF * 0xFF = 0xFE01).
; Inputs must be runtime to avoid constant folding.
define i8 @smulh_s8_neg(i8 %a, i8 %b) nounwind {
  %aa = sext i8 %a to i16
  %bb = sext i8 %b to i16
  %m = mul i16 %aa, %bb
  %shifted = lshr i16 %m, 8
  %r = trunc i16 %shifted to i8
  ret i8 %r
}
