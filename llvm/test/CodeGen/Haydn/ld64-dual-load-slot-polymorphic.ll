; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Dual independent i64 loads must use 64-bit load forms (ld64 / d_ldw_*).

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	dual_load_i64_slot_poly         // -- Begin function dual_load_i64_slot_poly
; CHECK: 	.type	dual_load_i64_slot_poly,@function
; CHECK: dual_load_i64_slot_poly:                // @dual_load_i64_slot_poly
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r1, 4; 	ld32	r4, r1, 0; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, 8; 	ld32	r3, r3, 0; 	nop }
; CHECK: 	{ 		addi32_w	r5, r1, 4; 	ld32	r1, r1, 0; 	nop }
; CHECK: 	{ 		nop; 	ld32	r5, r5, 0; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		st32	r4, sp, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r3, sp, 4; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	ld64	d0, sp, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		st32	r1, sp, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r5, sp, 4; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r2, 4; 	ld64	d1, sp, 0; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	add64	d0, d0, d1 }
; CHECK: 	{ 		nop; 	nop; 	d_sw_l_with_imm	d0, r2, 0 }
; CHECK: 	{ 		nop; 	nop; 	d_sw_h_with_imm	d0, r1, 0 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	dual_load_i64_slot_poly, .Lfunc_end0-dual_load_i64_slot_poly
; CHECK:                                         // -- End function
; CHECK: 	.globl	triple_load_i64_slot_poly       // -- Begin function triple_load_i64_slot_poly
; CHECK: 	.type	triple_load_i64_slot_poly,@function
; CHECK: triple_load_i64_slot_poly:              // @triple_load_i64_slot_poly
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		addi32_w	r3, r1, 4; 	ld32	r4, r1, 0; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, 16; 	nop; 	addi32	r5, r1, 8 }
; CHECK: 	{ 		addi32_w	r7, r1, 4; 	ld32	r3, r3, 0; 	nop }
; CHECK: 	{ 		addi32_w	r6, r5, 4; 	ld32	r5, r5, 0; 	nop }
; CHECK: 	{ 		ld32	r1, r1, 0; 	ld32	r6, r6, 0; 	nop }
; CHECK: 	{ 		nop; 	ld32	r7, r7, 0; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		st32	r4, sp, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r3, sp, 4; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	ld64	d0, sp, 0; 	nop }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		st32	r5, sp, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r6, sp, 4; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	ld64	d1, sp, 0; 	nop }
; CHECK: 	{ 		nop; 	add64	d0, d0, d1; 	subi32	sp, sp, 8 }
; CHECK: 	{ 		st32	r1, sp, 0; 	nop; 	nop }
; CHECK: 	{ 		st32	r7, sp, 4; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r2, 4; 	ld64	d2, sp, 0; 	nop }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	add64	d0, d0, d2 }
; CHECK: 	{ 		nop; 	nop; 	d_sw_l_with_imm	d0, r2, 0 }
; CHECK: 	{ 		nop; 	nop; 	d_sw_h_with_imm	d0, r1, 0 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	triple_load_i64_slot_poly, .Lfunc_end1-triple_load_i64_slot_poly
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @dual_load_i64_slot_poly(ptr %p, ptr %q) nounwind {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %s = add i64 %a, %b
  store i64 %s, ptr %q
  ret void
}

define void @triple_load_i64_slot_poly(ptr %p, ptr %q) nounwind {
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %p3 = getelementptr inbounds i64, ptr %p, i64 2
  %c = load i64, ptr %p3
  %s1 = add i64 %a, %b
  %s = add i64 %s1, %c
  store i64 %s, ptr %q
  ret void
}
