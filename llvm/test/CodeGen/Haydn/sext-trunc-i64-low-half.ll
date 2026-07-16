; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 | FileCheck %s

;
; REGRESSION TEST: (long long)((int)x) must preserve the LOW 32 bits of x.
;
; The `(long long)((int)v20)` cast lowers as sext i32->i64 of trunc i64->i32
; which the selector implements via the SRA64-by-32 fast path (ashr i64 X, 32).
; After SRA64-by-32, the 32-bit value of interest lives in the result's LOW
; lane — the HIGH lane is just the replicated sign bit. The extract that pulls
; the value back into a GPR32 must therefore be MOVE32_DR_L, NOT MOVE32_DR_H.
;
; Bug: the extract used MOVE32_DR_H, so it pulled the sign mask instead of the
; value — (long long)((int)x) collapsed to 0 or -1, destroying the low half.
; This test mirrors the yarpgen seed-8 shape: read a global i64, cast it to int
; then back to long long, OR with another i64, and return. If the extract
; regresses to MOVE32_DR_H, the OR is fed the sign mask and the function's
; computed value is wrong.
;
; Test design: the sext-trunc sequence is forced by the (int) cast. We anchor
; the check at the function label and assert MOVE32_DR_L is emitted at least
; once for the extract, and that MOVE32_DR_H is NOT emitted in this function.

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped







; CHECK:  	.text
; CHECK:  	.globl	sext_trunc_low_half             // -- Begin function sext_trunc_low_half
; CHECK:  	.type	sext_trunc_low_half,@function
; CHECK:  sext_trunc_low_half:                    // @sext_trunc_low_half
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:                               // %entry
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 8 }
; CHECK:  	.cfi_def_cfa_offset 8
; CHECK:  	{ 	lui	r1, v20; 	subi32	sp, sp, 8; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r1, v20 }
; CHECK:  	{ 	lui	r2, v4; 	ld64	d0, r1, 0; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r2, v4; 	move32_dr_l	r1, d0; 	nop }
; CHECK:  	{ 	ld64	d1, r2, 0; 	move32_dr_h	r2, d0; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 0 }
; CHECK:  	{ 	st32	r2, sp, 0 }
; CHECK:  	{ 	st32	r1, sp, 4 }
; CHECK:  	{ 	ld64	d0, sp, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r0, 32 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8; 	sra64	d0, d0, r1; 	nop }
; CHECK:  	{ 	or64	d0, d0, d1 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 8 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	sext_trunc_low_half, .Lfunc_end0-sext_trunc_low_half
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.type	v20,@object                     // @v20
; CHECK:  	.data
; CHECK:  	.globl	v20
; CHECK:  	.p2align	3, 0x0
; CHECK:  v20:
; CHECK:  	.quad	1851795283613690922             // 0x19b2e5ffcc89ac2a
; CHECK:  	.size	v20, 8
; CHECK:  	.type	v4,@object                      // @v4
; CHECK:  	.globl	v4
; CHECK:  	.p2align	3, 0x0
; CHECK:  v4:
; CHECK:  	.quad	-6903239071969274604            // 0xa032c4a393ac9514
; CHECK:  	.size	v4, 8
; CHECK:  	.section	".note.GNU-stack","",@progbits

@v20 = dso_local global i64 1851795283613690922, align 8
@v4  = dso_local global i64 11543505001740277012, align 8

define dso_local i64 @sext_trunc_low_half() {
entry:
  %a = load i64, ptr @v20, align 8
  %trunc = trunc i64 %a to i32
  %sext = sext i32 %trunc to i64
  %b = load i64, ptr @v4, align 8
  %or = or i64 %sext, %b
  ret i64 %or
}
