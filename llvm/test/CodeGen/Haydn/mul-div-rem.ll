; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Multiply / divide / remainder selection smoke.
; s32 mul → MULL; s64 mul → native MUL64 schoolbook or partials; div/rem → libcalls.

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	mul_i32                         // -- Begin function mul_i32
; CHECK: 	.type	mul_i32,@function
; CHECK: mul_i32:                                // @mul_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ mull	r2, r1, r2; nop; nop }
; CHECK: 	{ move32	r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	mul_i32, .Lfunc_end0-mul_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	mul_i32_const                   // -- Begin function mul_i32_const
; CHECK: 	.type	mul_i32_const,@function
; CHECK: mul_i32_const:                          // @mul_i32_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r2, r0, 42 }
; CHECK: 	{ mull	r2, r1, r2; nop; nop }
; CHECK: 	{ move32	r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	mul_i32_const, .Lfunc_end1-mul_i32_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	mul_i64_with_use                // -- Begin function mul_i64_with_use
; CHECK: 	.type	mul_i64_with_use,@function
; CHECK: mul_i64_with_use:                       // @mul_i64_with_use
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ subi32	sp, sp, 8; nop; addi32_w	r1, r0, 1 }
; CHECK: 	{ move32_dr_h	r2, d0; move32_dr_l	r3, d1; st32	r1, sp, 0 }
; CHECK: 	{ move32_dr_h	r4, d1; sext32t64	d1, r2; nop }
; CHECK: 	{ slli64	d1, d1, 32; sext32t64	d3, r3; addi32_w	r1, r0, 0 }
; CHECK: 	{ slli64	d3, d3, 32; sext32t64	d4, r4; st32	r1, sp, 4 }
; CHECK: 	{ slli64	d4, d4, 32; ld64	d2, sp, 0; addi32_w	sp, sp, 8 }
; CHECK: 	{ move32_dr_l	r1, d0; srli64	d1, d1, 32; nop }
; CHECK: 	{ srli64	d3, d3, 32; sext32t64	d0, r1; addi32_w	r1, r0, 32 }
; CHECK: 	{ slli64	d0, d0, 32; srli64	d4, d4, 32; nop }
; CHECK: 	{ srli64	d0, d0, 32; mul64.ulul	d1, d1, d3; nop }
; CHECK: 	{ mul64.ulul	d5, d0, d3; mul64.ulul	d0, d0, d4; nop }
; CHECK: 	{ add64	d0, d0, d1; nop; nop }
; CHECK: 	{ sll64	d0, d0, r1; nop; nop }
; CHECK: 	{ add64	d0, d5, d0; nop; nop }
; CHECK: 	{ add64	d0, d0, d2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	mul_i64_with_use, .Lfunc_end2-mul_i64_with_use
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	sdiv_i32                        // -- Begin function sdiv_i32
; CHECK: 	.type	sdiv_i32,@function
; CHECK: sdiv_i32:                               // @sdiv_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal_w	lr, __divsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	sdiv_i32, .Lfunc_end3-sdiv_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	udiv_i32                        // -- Begin function udiv_i32
; CHECK: 	.type	udiv_i32,@function
; CHECK: udiv_i32:                               // @udiv_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal_w	lr, __udivsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	udiv_i32, .Lfunc_end4-udiv_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	srem_i32                        // -- Begin function srem_i32
; CHECK: 	.type	srem_i32,@function
; CHECK: srem_i32:                               // @srem_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal_w	lr, __modsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	srem_i32, .Lfunc_end5-srem_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	urem_i32                        // -- Begin function urem_i32
; CHECK: 	.type	urem_i32,@function
; CHECK: urem_i32:                               // @urem_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal_w	lr, __umodsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	urem_i32, .Lfunc_end6-urem_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	sdiv_const                      // -- Begin function sdiv_const
; CHECK: 	.type	sdiv_const,@function
; CHECK: sdiv_const:                             // @sdiv_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ srai32	r2, r1, 31; nop; addi32_w	r3, r0, 0 }
; CHECK: 	{ srli32	r2, r2, 29; nop; nop }
; CHECK: 	{ add32	r2, r1, r2; nop; nop }
; CHECK: 	{ srai32	r2, r2, 3; nop; nop }
; CHECK: 	{ movt32	r2, r1, r3; nop; nop }
; CHECK: 	{ sub32	r1, r3, r2; nop; nop }
; CHECK: 	{ movt32	r2, r1, r3; nop; nop }
; CHECK: 	{ move32	r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	sdiv_const, .Lfunc_end7-sdiv_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	udiv_const                      // -- Begin function udiv_const
; CHECK: 	.type	udiv_const,@function
; CHECK: udiv_const:                             // @udiv_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r2, 3277 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, -209715 }
; CHECK: 	{ muluuh	r2, r1, r2; nop; addi32_w	r3, r0, 0 }
; CHECK: 	{ nop; nop; nop }
; CHECK: 	{ srli32	r2, r2, 3; nop; nop }
; CHECK: 	{ movt32	r2, r1, r3; nop; nop }
; CHECK: 	{ move32	r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	udiv_const, .Lfunc_end8-udiv_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	srem_const                      // -- Begin function srem_const
; CHECK: 	.type	srem_const,@function
; CHECK: srem_const:                             // @srem_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r2, 2341 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, -449389 }
; CHECK: 	{ mulssh	r2, r1, r2; nop; nop }
; CHECK: 	{ nop; nop; nop }
; CHECK: 	{ add32	r2, r2, r1; nop; nop }
; CHECK: 	{ srai32	r2, r2, 2; nop; nop }
; CHECK: 	{ srli32	r3, r2, 31; nop; nop }
; CHECK: 	{ add32	r2, r2, r3; nop; nop }
; CHECK: 	{ slli32	r3, r2, 3; nop; nop }
; CHECK: 	{ sub32	r2, r3, r2; nop; nop }
; CHECK: 	{ sub32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	srem_const, .Lfunc_end9-srem_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	mul_then_div                    // -- Begin function mul_then_div
; CHECK: 	.type	mul_then_div,@function
; CHECK: mul_then_div:                           // @mul_then_div
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ mull	r2, r1, r2; nop; nop }
; CHECK: 	{ move32	r1, r2; move32	r2, r3; nop }
; CHECK: 	{ nop; nop; jal_w	lr, __divsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	mul_then_div, .Lfunc_end10-mul_then_div
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	div_then_mul                    // -- Begin function div_then_mul
; CHECK: 	.type	div_then_mul,@function
; CHECK: div_then_mul:                           // @div_then_mul
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	r4, sp, 8 }
; CHECK: 	{ nop; nop; st32	lr, r4, 0 }
; CHECK: 	{ nop; nop; st32	r8, r4, 4 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset r8, 12
; CHECK: 	.cfi_offset lr, 8
; CHECK: 	{ move32	r8, r3; nop; nop }
; CHECK: 	{ nop; nop; jal_w	lr, __divsi3 }
; CHECK: 	{ xor32	r0, r0, r0; mull	r8, r1, r8; nop }
; CHECK: 	{ move32	r1, r8; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 8; nop }
; CHECK: 	{ nop; ld32	r8, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	div_then_mul, .Lfunc_end11-div_then_mul
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	modulo_i32                      // -- Begin function modulo_i32
; CHECK: 	.type	modulo_i32,@function
; CHECK: modulo_i32:                             // @modulo_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal_w	lr, __modsi3 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	modulo_i32, .Lfunc_end12-modulo_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	abs_i32                         // -- Begin function abs_i32
; CHECK: 	.type	abs_i32,@function
; CHECK: abs_i32:                                // @abs_i32
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r2, r0, 0 }
; CHECK: 	{ slt32	r3, r1, r2; sub32	r2, r2, r1; nop }
; CHECK: 	{ movt32	r1, r2, r3; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	abs_i32, .Lfunc_end13-abs_i32
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	mul_add                         // -- Begin function mul_add
; CHECK: 	.type	mul_add,@function
; CHECK: mul_add:                                // @mul_add
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ mull	r2, r1, r2; nop; nop }
; CHECK: 	{ add32	r1, r2, r3; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	mul_add, .Lfunc_end14-mul_add
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	complex_arith                   // -- Begin function complex_arith
; CHECK: 	.type	complex_arith,@function
; CHECK: complex_arith:                          // @complex_arith
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 24; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	r4, sp, 8 }
; CHECK: 	{ nop; nop; st32	lr, r4, 0 }
; CHECK: 	{ nop; nop; st32	r10, r4, 4 }
; CHECK: 	{ nop; nop; st32	r9, r4, 8 }
; CHECK: 	{ nop; nop; st32	r8, r4, 12 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	.cfi_offset r8, 20
; CHECK: 	.cfi_offset r9, 16
; CHECK: 	.cfi_offset r10, 12
; CHECK: 	.cfi_offset lr, 8
; CHECK: 	{ move32	r9, r1; move32	r8, r3; nop }
; CHECK: 	{ mull	r2, r9, r2; nop; nop }
; CHECK: 	{ move32	r1, r2; move32	r2, r8; nop }
; CHECK: 	{ nop; nop; jal_w	lr, __divsi3 }
; CHECK: 	{ move32	r10, r1; move32	r1, r9; nop }
; CHECK: 	{ move32	r2, r8; xor32	r0, r0, r0; nop }
; CHECK: 	{ nop; nop; jal_w	lr, __modsi3 }
; CHECK: 	{ xor32	r0, r0, r0; add32	r1, r10, r1; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 8; nop }
; CHECK: 	{ nop; ld32	r10, sp, 12; nop }
; CHECK: 	{ nop; ld32	r9, sp, 16; nop }
; CHECK: 	{ nop; ld32	r8, sp, 20; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 24 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	complex_arith, .Lfunc_end15-complex_arith
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @mul_i32(i32 %a, i32 %b) {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


  %r = mul i32 %a, %b
  ret i32 %r
}

define i32 @mul_i32_const(i32 %a) {
  %r = mul i32 %a, 42
  ret i32 %r
}

define i64 @mul_i64_with_use(i64 %a, i64 %b) {
; Schoolbook / native partials (T7.5: not a libcall).
  %r = mul i64 %a, %b
  %r2 = add i64 %r, 1
  ret i64 %r2
}

define i32 @sdiv_i32(i32 %a, i32 %b) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @udiv_i32(i32 %a, i32 %b) {
  %r = udiv i32 %a, %b
  ret i32 %r
}

define i32 @srem_i32(i32 %a, i32 %b) {
  %r = srem i32 %a, %b
  ret i32 %r
}

define i32 @urem_i32(i32 %a, i32 %b) {
  %r = urem i32 %a, %b
  ret i32 %r
}

define i32 @sdiv_const(i32 %a) {
  %r = sdiv i32 %a, 8
  ret i32 %r
}

define i32 @udiv_const(i32 %a) {
; Magic high-multiply via MULUUH (G_UMULH s32 Pat).
  %r = udiv i32 %a, 10
  ret i32 %r
}

define i32 @srem_const(i32 %a) {
; May strength-reduce (and/mask) or call __modsi3.
  %r = srem i32 %a, 7
  ret i32 %r
}

define i32 @mul_then_div(i32 %a, i32 %b, i32 %c) {
  %prod = mul i32 %a, %b
  %r = sdiv i32 %prod, %c
  ret i32 %r
}

define i32 @div_then_mul(i32 %a, i32 %b, i32 %c) {
  %quot = sdiv i32 %a, %b
  %r = mul i32 %quot, %c
  ret i32 %r
}

define i32 @modulo_i32(i32 %a, i32 %n) {
  %r = srem i32 %a, %n
  ret i32 %r
}

define i32 @abs_i32(i32 %a) {
; Select-form abs (not llvm.abs → ABS32): SLT + SUB + MOVT (T7.5 polarity).
  %cmp = icmp slt i32 %a, 0
  %neg = sub i32 0, %a
  %r = select i1 %cmp, i32 %neg, i32 %a
  ret i32 %r
}

define i32 @mul_add(i32 %a, i32 %b, i32 %c) {
  %prod = mul i32 %a, %b
  %r = add i32 %prod, %c
  ret i32 %r
}

define i32 @complex_arith(i32 %a, i32 %b, i32 %c) {
  %prod = mul i32 %a, %b
  %quot = sdiv i32 %prod, %c
  %rem = srem i32 %a, %c
  %r = add i32 %quot, %rem
  ret i32 %r
}
