; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; G_MUL s32 → MULL (golden MAC GRR). CHECK-NOT phantom mul32 / libcall.

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	scalar_mul                      // -- Begin function scalar_mul
; CHECK: 	.type	scalar_mul,@function
; CHECK: scalar_mul:                             // @scalar_mul
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	mull	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	scalar_mul, .Lfunc_end0-scalar_mul
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	cond_mul                        // -- Begin function cond_mul
; CHECK: 	.type	cond_mul,@function
; CHECK: cond_mul:                               // @cond_mul
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r4, r0, 1; 	nop; 	mull	r3, r2, r3 }
; CHECK: 	{ 		addi32_w	r5, r0, 0; 	nop; 	and32	r1, r1, r4 }
; CHECK: 	{ 		nop; 	nop; 	seq32	r1, r1, r5 }
; CHECK: 	{ 		nop; 	nop; 	movt32	r3, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	move32	r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	cond_mul, .Lfunc_end1-cond_mul
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @scalar_mul(i32 %a, i32 %b) {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


entry:
  %m = mul i32 %a, %b
  ret i32 %m
}

; Conditional multiply (shape: `if (n & 1) h = h * x;`).
define i32 @cond_mul(i32 %n, i32 %h, i32 %x) {
entry:
  %bit = and i32 %n, 1
  %cmp = icmp eq i32 %bit, 0
  %mh = mul i32 %h, %x
  %sel = select i1 %cmp, i32 %h, i32 %mh
  ret i32 %sel
}
