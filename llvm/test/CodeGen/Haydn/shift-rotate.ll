; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.


;
; Test shift and rotate operations for Haydn.
; Complements the basic shift.ll test with edge cases, constant shifts
; shift+mask patterns, and sign-extension-via-shift idioms.

;Shift by constant amounts

; Shift by 1 (smallest non-zero)
; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	shl_by_1                        // -- Begin function shl_by_1
; CHECK: 	.type	shl_by_1,@function
; CHECK: shl_by_1:                               // @shl_by_1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 1; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	shl_by_1, .Lfunc_end0-shl_by_1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lshr_by_1                       // -- Begin function lshr_by_1
; CHECK: 	.type	lshr_by_1,@function
; CHECK: lshr_by_1:                              // @lshr_by_1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 1; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	lshr_by_1, .Lfunc_end1-lshr_by_1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	ashr_by_1                       // -- Begin function ashr_by_1
; CHECK: 	.type	ashr_by_1,@function
; CHECK: ashr_by_1:                              // @ashr_by_1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 1; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sra32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	ashr_by_1, .Lfunc_end2-ashr_by_1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	shl_by_0                        // -- Begin function shl_by_0
; CHECK: 	.type	shl_by_0,@function
; CHECK: shl_by_0:                               // @shl_by_0
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	shl_by_0, .Lfunc_end3-shl_by_0
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	shl_by_31                       // -- Begin function shl_by_31
; CHECK: 	.type	shl_by_31,@function
; CHECK: shl_by_31:                              // @shl_by_31
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 31; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	shl_by_31, .Lfunc_end4-shl_by_31
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lshr_by_31                      // -- Begin function lshr_by_31
; CHECK: 	.type	lshr_by_31,@function
; CHECK: lshr_by_31:                             // @lshr_by_31
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 31; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	lshr_by_31, .Lfunc_end5-lshr_by_31
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	ashr_by_31                      // -- Begin function ashr_by_31
; CHECK: 	.type	ashr_by_31,@function
; CHECK: ashr_by_31:                             // @ashr_by_31
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 31; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sra32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	ashr_by_31, .Lfunc_end6-ashr_by_31
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	shl_by_5                        // -- Begin function shl_by_5
; CHECK: 	.type	shl_by_5,@function
; CHECK: shl_by_5:                               // @shl_by_5
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 5; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	shl_by_5, .Lfunc_end7-shl_by_5
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	shl_variable                    // -- Begin function shl_variable
; CHECK: 	.type	shl_variable,@function
; CHECK: shl_variable:                           // @shl_variable
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	shl_variable, .Lfunc_end8-shl_variable
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lshr_variable                   // -- Begin function lshr_variable
; CHECK: 	.type	lshr_variable,@function
; CHECK: lshr_variable:                          // @lshr_variable
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	lshr_variable, .Lfunc_end9-lshr_variable
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	ashr_variable                   // -- Begin function ashr_variable
; CHECK: 	.type	ashr_variable,@function
; CHECK: ashr_variable:                          // @ashr_variable
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		nop; 	nop; 	sra32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	ashr_variable, .Lfunc_end10-ashr_variable
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	sext_via_shift_16               // -- Begin function sext_via_shift_16
; CHECK: 	.type	sext_via_shift_16,@function
; CHECK: sext_via_shift_16:                      // @sext_via_shift_16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 16; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	sra32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	sext_via_shift_16, .Lfunc_end11-sext_via_shift_16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	sext_via_shift_24               // -- Begin function sext_via_shift_24
; CHECK: 	.type	sext_via_shift_24,@function
; CHECK: sext_via_shift_24:                      // @sext_via_shift_24
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 24; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	sra32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	sext_via_shift_24, .Lfunc_end12-sext_via_shift_24
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	shift_multi_use                 // -- Begin function shift_multi_use
; CHECK: 	.type	shift_multi_use,@function
; CHECK: shift_multi_use:                        // @shift_multi_use
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r3, r0, 2; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	sll32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	add32	r2, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	mull	r1, r2, r1 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	shift_multi_use, .Lfunc_end13-shift_multi_use
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	extract_byte1                   // -- Begin function extract_byte1
; CHECK: 	.type	extract_byte1,@function
; CHECK: extract_byte1:                          // @extract_byte1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 8; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r3, r0, 255; 	nop; 	srl32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	and32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	extract_byte1, .Lfunc_end14-extract_byte1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	merge_halves                    // -- Begin function merge_halves
; CHECK: 	.type	merge_halves,@function
; CHECK: merge_halves:                           // @merge_halves
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r3, r0, 65535; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r4, r0, 16; 	nop; 	and32	r1, r1, r3 }
; CHECK: 	{ 		nop; 	nop; 	sll32	r2, r2, r4 }
; CHECK: 	{ 		nop; 	nop; 	or32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	merge_halves, .Lfunc_end15-merge_halves
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @shl_by_1(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

define i32 @lshr_by_1(i32 %x) {
  %r = lshr i32 %x, 1
  ret i32 %r
}

define i32 @ashr_by_1(i32 %x) {
  %r = ashr i32 %x, 1
  ret i32 %r
}

; Shift by 0 (identity — optimizer eliminates the shift entirely)
define i32 @shl_by_0(i32 %x) {
  %r = shl i32 %x, 0
  ret i32 %r
}

; Shift by 31 (largest meaningful shift for i32)
define i32 @shl_by_31(i32 %x) {
  %r = shl i32 %x, 31
  ret i32 %r
}

define i32 @lshr_by_31(i32 %x) {
  %r = lshr i32 %x, 31
  ret i32 %r
}

define i32 @ashr_by_31(i32 %x) {
  %r = ashr i32 %x, 31
  ret i32 %r
}

; Shift by 5 (mid-range constant)
define i32 @shl_by_5(i32 %x) {
  %r = shl i32 %x, 5
  ret i32 %r
}

;Variable shifts

define i32 @shl_variable(i32 %x, i32 %s) {
  %r = shl i32 %x, %s
  ret i32 %r
}

define i32 @lshr_variable(i32 %x, i32 %s) {
  %r = lshr i32 %x, %s
  ret i32 %r
}

define i32 @ashr_variable(i32 %x, i32 %s) {
  %r = ashr i32 %x, %s
  ret i32 %r
}

;Sign-extension via shift (common C pattern: (x << 16) >> 16)

define i32 @sext_via_shift_16(i32 %x) {
  %shl = shl i32 %x, 16
  %shr = ashr i32 %shl, 16
  ret i32 %shr
}

define i32 @sext_via_shift_24(i32 %x) {
  %shl = shl i32 %x, 24
  %shr = ashr i32 %shl, 24
  ret i32 %shr
}

;Shift with multiple uses (should not be duplicated)

define i32 @shift_multi_use(i32 %x, i32 %y) {
  %a = shl i32 %x, 2
  %b = add i32 %a, %y
  %c = mul i32 %b, %a
  ret i32 %c
}

;Shift+mask patterns (byte/halfword extraction)

; Extract byte 1: (x >> 8) & 0xFF
define i32 @extract_byte1(i32 %x) {
  %shr = lshr i32 %x, 8
  %and = and i32 %shr, 255
  ret i32 %and
}

; Merge two 16-bit fields into 32 bits: (lo & 0xFFFF) | (hi << 16)
define i32 @merge_halves(i32 %lo, i32 %hi) {
  %lo_masked = and i32 %lo, 65535
  %hi_shifted = shl i32 %hi, 16
  %result = or i32 %lo_masked, %hi_shifted
  ret i32 %result
}
