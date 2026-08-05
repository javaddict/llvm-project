; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;Comprehensive tests for large immediate constant materialization.
;ISA-43 #1 / : LUI is 12-bit (sets bits[31:20]); ADDI32 is simm16;
;ORI32 is uimm16. The materialiser picks:
;- simm16 -> 1 ADDI32 (from R0)
;- uimm16 (32768..65535) -> 1 ORI32 (from R0)
;- round-trips through pair -> `lui hi12` + `addi32{{(_w)?}} simm20`
;- low16 == 0 (upper-only) -> `lui hi12` + `addi32{{(_w)?}} simm20`
;Every sequence below was verified by reconstructing the value from R0:
;hi12 = (V + 0x80000) >> 20, simm20 = V - (hi12 << 20).

;Full 32-bit constants that round-trip -> lui + addi32{{(_w)?}} (2 instrs)

; 0x1234ABCD
; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	lo16_bit15_set_1                // -- Begin function lo16_bit15_set_1
; CHECK: 	.type	lo16_bit15_set_1,@function
; CHECK: lo16_bit15_set_1:                       // @lo16_bit15_set_1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 291 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 306125 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	lo16_bit15_set_1, .Lfunc_end0-lo16_bit15_set_1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	int_max                         // -- Begin function int_max
; CHECK: 	.type	int_max,@function
; CHECK: int_max:                                // @int_max
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 2048 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, -1 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	int_max, .Lfunc_end1-int_max
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	hi_zero_lo_all_ones             // -- Begin function hi_zero_lo_all_ones
; CHECK: 	.type	hi_zero_lo_all_ones,@function
; CHECK: hi_zero_lo_all_ones:                    // @hi_zero_lo_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 65535 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	hi_zero_lo_all_ones, .Lfunc_end2-hi_zero_lo_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	negative_lo_min                 // -- Begin function negative_lo_min
; CHECK: 	.type	negative_lo_min,@function
; CHECK: negative_lo_min:                        // @negative_lo_min
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, -32768 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	negative_lo_min, .Lfunc_end3-negative_lo_min
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lo16_just_above_bit15           // -- Begin function lo16_just_above_bit15
; CHECK: 	.type	lo16_just_above_bit15,@function
; CHECK: lo16_just_above_bit15:                  // @lo16_just_above_bit15
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 2749 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, -163839 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	lo16_just_above_bit15, .Lfunc_end4-lo16_just_above_bit15
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lo16_all_ones                   // -- Begin function lo16_all_ones
; CHECK: 	.type	lo16_all_ones,@function
; CHECK: lo16_all_ones:                          // @lo16_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 3563 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, -131073 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	lo16_all_ones, .Lfunc_end5-lo16_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lo_zero                         // -- Begin function lo_zero
; CHECK: 	.type	lo_zero,@function
; CHECK: lo_zero:                                // @lo_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 291 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 262144 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	lo_zero, .Lfunc_end6-lo_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	lo16_no_bit15                   // -- Begin function lo16_no_bit15
; CHECK: 	.type	lo16_no_bit15,@function
; CHECK: lo16_no_bit15:                          // @lo16_no_bit15
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 291 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 266804 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	lo16_no_bit15, .Lfunc_end7-lo16_no_bit15
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	max_simm16                      // -- Begin function max_simm16
; CHECK: 	.type	max_simm16,@function
; CHECK: max_simm16:                             // @max_simm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 32767 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	max_simm16, .Lfunc_end8-max_simm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	zero                            // -- Begin function zero
; CHECK: 	.type	zero,@function
; CHECK: zero:                                   // @zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	zero, .Lfunc_end9-zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	small_pos                       // -- Begin function small_pos
; CHECK: 	.type	small_pos,@function
; CHECK: small_pos:                              // @small_pos
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 42 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	small_pos, .Lfunc_end10-small_pos
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	small_neg                       // -- Begin function small_neg
; CHECK: 	.type	small_neg,@function
; CHECK: small_neg:                              // @small_neg
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, -42 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	small_neg, .Lfunc_end11-small_neg
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	min_simm16                      // -- Begin function min_simm16
; CHECK: 	.type	min_simm16,@function
; CHECK: min_simm16:                             // @min_simm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, -32768 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	min_simm16, .Lfunc_end12-min_simm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	add_with_lo16_bit15             // -- Begin function add_with_lo16_bit15
; CHECK: 	.type	add_with_lo16_bit15,@function
; CHECK: add_with_lo16_bit15:                    // @add_with_lo16_bit15
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r2, 291 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, 306125 }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	add_with_lo16_bit15, .Lfunc_end13-add_with_lo16_bit15
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	cmp_with_int_max                // -- Begin function cmp_with_int_max
; CHECK: 	.type	cmp_with_int_max,@function
; CHECK: cmp_with_int_max:                       // @cmp_with_int_max
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r2, 2048 }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, -1 }
; CHECK: 	{ slt32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr_w	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	cmp_with_int_max, .Lfunc_end14-cmp_with_int_max
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @lo16_bit15_set_1() {
; Flex cutover: lui -> lui (slot-suffixed Flex form).

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


  ret i32 305441741
}

; 0x7FFFFFFF (INT_MAX): LUI 0x800 + ADDI32_W -1
define i32 @int_max() {
  ret i32 2147483647
}

; 0x0000FFFF: fits in simm20 -> single ADDI32_W from R0 (WIDE-only MatInt)
define i32 @hi_zero_lo_all_ones() {
  ret i32 65535
}

; 0xFFFF8000 = -32768: fits simm20 -> single ADDI32_W from R0
define i32 @negative_lo_min() {
  ret i32 -32768
}

; 0xAC258001
define i32 @lo16_just_above_bit15() {
  ret i32 2882371585
}

; 0xDEADFFFF
define i32 @lo16_all_ones() {
  ret i32 3735945215
}

;Upper-half-only (low 16 bits zero) -> lui + addi32{{(_w)?}} (canonical pair)

; 0x12340000
define i32 @lo_zero() {
  ret i32 305397760
}

; 0x12341234: low16 != 0 -> lui + addi32{{(_w)?}}
define i32 @lo16_no_bit15() {
  ret i32 305402420
}

; 0x00007FFF: max positive simm16 -> single ADDI32_W (simm20 covers simm16)
define i32 @max_simm16() {
  ret i32 32767
}

;Small constants (single ADDI32_W)

define i32 @zero() {
  ret i32 0
}

define i32 @small_pos() {
  ret i32 42
}

define i32 @small_neg() {
  ret i32 -42
}

define i32 @min_simm16() {
  ret i32 -32768
}

;Constants used in arithmetic

define i32 @add_with_lo16_bit15(i32 %a) {
  %1 = add i32 %a, 305441741
  ret i32 %1
}

define i1 @cmp_with_int_max(i32 %a) {
  %1 = icmp slt i32 %a, 2147483647
  ret i1 %1
}
