; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s




; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	const_zero                      // -- Begin function const_zero
; CHECK: 	.type	const_zero,@function
; CHECK: const_zero:                             // @const_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	const_zero, .Lfunc_end0-const_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_small                     // -- Begin function const_small
; CHECK: 	.type	const_small,@function
; CHECK: const_small:                            // @const_small
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 42; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	const_small, .Lfunc_end1-const_small
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_large                     // -- Begin function const_large
; CHECK: 	.type	const_large,@function
; CHECK: const_large:                            // @const_large
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		lui	r1, 12; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, -237234; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	const_large, .Lfunc_end2-const_large
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @const_zero() {
  ret i32 0
}

define i32 @const_small() {
  ret i32 42
}

define i32 @const_large() {
  ret i32 12345678
}
