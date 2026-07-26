; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Narrow high-half multiply patterns (yarpgen seeds 1, 6, 10).
; s8/s16 high product via extend + MULL + shift.

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	smulh_s8                        // -- Begin function smulh_s8
; CHECK: 	.type	smulh_s8,@function
; CHECK: smulh_s8:                               // @smulh_s8
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r0, 24; 	nop; 	nop }
; CHECK: 	{ 		nop; 	sll32	r2, r2, r3; 	sll32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	sra32	r2, r2, r3; 	sra32	r1, r1, r3 }
; CHECK: 	{ 		addi32_w	r1, r0, 65535; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		addi32_w	r2, r0, 8; 	nop; 	and32	r1, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	smulh_s8, .Lfunc_end0-smulh_s8
; CHECK:                                         // -- End function
; CHECK: 	.globl	umulh_s8                        // -- Begin function umulh_s8
; CHECK: 	.type	umulh_s8,@function
; CHECK: umulh_s8:                               // @umulh_s8
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r0, 255; 	nop; 	nop }
; CHECK: 	{ 		nop; 	and32	r1, r1, r3; 	and32	r2, r2, r3 }
; CHECK: 	{ 		addi32_w	r1, r0, 8; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	umulh_s8, .Lfunc_end1-umulh_s8
; CHECK:                                         // -- End function
; CHECK: 	.globl	smulh_s16                       // -- Begin function smulh_s16
; CHECK: 	.type	smulh_s16,@function
; CHECK: smulh_s16:                              // @smulh_s16
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r0, 16; 	nop; 	nop }
; CHECK: 	{ 		nop; 	sll32	r2, r2, r3; 	sll32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	sra32	r1, r1, r3; 	sra32	r2, r2, r3 }
; CHECK: 	{ 		nop; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r2, r3 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	smulh_s16, .Lfunc_end2-smulh_s16
; CHECK:                                         // -- End function
; CHECK: 	.globl	umulh_s16                       // -- Begin function umulh_s16
; CHECK: 	.type	umulh_s16,@function
; CHECK: umulh_s16:                              // @umulh_s16
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r4, r0, 65535; 	nop; 	nop }
; CHECK: 	{ 		nop; 	and32	r1, r1, r4; 	and32	r2, r2, r4 }
; CHECK: 	{ 		addi32_w	r3, r0, 16; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r2, r3 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	umulh_s16, .Lfunc_end3-umulh_s16
; CHECK:                                         // -- End function
; CHECK: 	.globl	smulh_s8_neg                    // -- Begin function smulh_s8_neg
; CHECK: 	.type	smulh_s8_neg,@function
; CHECK: smulh_s8_neg:                           // @smulh_s8_neg
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r0, 24; 	nop; 	nop }
; CHECK: 	{ 		nop; 	sll32	r2, r2, r3; 	sll32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	sra32	r2, r2, r3; 	sra32	r1, r1, r3 }
; CHECK: 	{ 		addi32_w	r1, r0, 65535; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		addi32_w	r2, r0, 8; 	nop; 	and32	r1, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	smulh_s8_neg, .Lfunc_end4-smulh_s8_neg
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i8 @smulh_s8(i8 %a, i8 %b) nounwind {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


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

define i8 @smulh_s8_neg(i8 %a, i8 %b) nounwind {
  %aa = sext i8 %a to i16
  %bb = sext i8 %b to i16
  %m = mul i16 %aa, %bb
  %shifted = lshr i16 %m, 8
  %r = trunc i16 %shifted to i8
  ret i8 %r
}
