; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Two independent i64 loads: expect 64-bit load forms (ld64 / d_ldw_*),
; not two slot-1-only conflicts that force separate issue.

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	dual_load_i64                   // -- Begin function dual_load_i64
; CHECK: 	.type	dual_load_i64,@function
; CHECK: dual_load_i64:                          // @dual_load_i64
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	d_ldw_post_imm	d0, r1, 1 }
; CHECK: 	{ 		nop; 	ld64	d1, r1, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	add64	d0, d0, d1 }
; CHECK: 	{ 		st64	d0, r2, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	dual_load_i64, .Lfunc_end0-dual_load_i64
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @dual_load_i64(ptr %a, ptr %b) {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


entry:
  %x = load i64, ptr %a, align 8
  %ap = getelementptr i64, ptr %a, i32 1
  %y = load i64, ptr %ap, align 8
  %s = add i64 %x, %y
  store i64 %s, ptr %b, align 8
  ret void
}
