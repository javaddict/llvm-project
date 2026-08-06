; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: Constant materialisation via HaydnMatInt (ISA-43 #1 /).
;
; The Haydn ISA LUI is 12-bit-shift: LUI rt,imm12 -> rt = imm12 << 20 (bits
; [31:20]); ADDI32 is simm16; ORI32 is uimm16. The selector chains each sequence
; from R0 (soft-zero = 0): ADDI32 rd,R0,imm = sext(imm); ORI32 rd,R0,imm = imm;
; LUI rd,R0,imm = imm12<<20. So:
; simm16 -> 1 ADDI32
; 0..65535 -> 1 ORI32 (from R0)
; >1MiB / upper-only -> LUI [+ ADDI32] (1-2 instrs)
; everything else -> `lui hi12` + `addi32{{(_w)?}} simm20` (12+20 split covers
; the whole 32-bit space; canonical pair post-ASO-removal)
; Every constant below was verified by simulating the emitted sequence from R0
; and confirming it reconstructs the exact 32-bit value.

;================================================================
;Power-of-2 / small constants
;================================================================

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	const_pow2_2                    // -- Begin function const_pow2_2
; CHECK: 	.type	const_pow2_2,@function
; CHECK: const_pow2_2:                           // @const_pow2_2
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 2 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	const_pow2_2, .Lfunc_end0-const_pow2_2
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_pow2_0x100000             // -- Begin function const_pow2_0x100000
; CHECK: 	.type	const_pow2_0x100000,@function
; CHECK: const_pow2_0x100000:                    // @const_pow2_0x100000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 1 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	const_pow2_0x100000, .Lfunc_end1-const_pow2_0x100000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0x80000000                // -- Begin function const_0x80000000
; CHECK: 	.type	const_0x80000000,@function
; CHECK: const_0x80000000:                       // @const_0x80000000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 2048 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	const_0x80000000, .Lfunc_end2-const_0x80000000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0xF0000000                // -- Begin function const_0xF0000000
; CHECK: 	.type	const_0xF0000000,@function
; CHECK: const_0xF0000000:                       // @const_0xF0000000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 3840 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	const_0xF0000000, .Lfunc_end3-const_0xF0000000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_upper_0xFFFF0000          // -- Begin function const_upper_0xFFFF0000
; CHECK: 	.type	const_upper_0xFFFF0000,@function
; CHECK: const_upper_0xFFFF0000:                 // @const_upper_0xFFFF0000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, -65536 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	const_upper_0xFFFF0000, .Lfunc_end4-const_upper_0xFFFF0000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_upper_0x00010000          // -- Begin function const_upper_0x00010000
; CHECK: 	.type	const_upper_0x00010000,@function
; CHECK: const_upper_0x00010000:                 // @const_upper_0x00010000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 65536 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	const_upper_0x00010000, .Lfunc_end5-const_upper_0x00010000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_simm16_max                // -- Begin function const_simm16_max
; CHECK: 	.type	const_simm16_max,@function
; CHECK: const_simm16_max:                       // @const_simm16_max
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 32767 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	const_simm16_max, .Lfunc_end6-const_simm16_max
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_simm16_min                // -- Begin function const_simm16_min
; CHECK: 	.type	const_simm16_min,@function
; CHECK: const_simm16_min:                       // @const_simm16_min
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, -32768 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	const_simm16_min, .Lfunc_end7-const_simm16_min
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0x8000                    // -- Begin function const_0x8000
; CHECK: 	.type	const_0x8000,@function
; CHECK: const_0x8000:                           // @const_0x8000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 32768 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	const_0x8000, .Lfunc_end8-const_0x8000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0xFFFF                    // -- Begin function const_0xFFFF
; CHECK: 	.type	const_0xFFFF,@function
; CHECK: const_0xFFFF:                           // @const_0xFFFF
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 65535 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	const_0xFFFF, .Lfunc_end9-const_0xFFFF
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0x12345678                // -- Begin function const_0x12345678
; CHECK: 	.type	const_0x12345678,@function
; CHECK: const_0x12345678:                       // @const_0x12345678
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 291 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, 284280 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	const_0x12345678, .Lfunc_end10-const_0x12345678
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0xDEADBEEF                // -- Begin function const_0xDEADBEEF
; CHECK: 	.type	const_0xDEADBEEF,@function
; CHECK: const_0xDEADBEEF:                       // @const_0xDEADBEEF
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; lui	r1, 3563 }
; CHECK: 	{ nop; nop; addi32_w	r1, r1, -147729 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	const_0xDEADBEEF, .Lfunc_end11-const_0xDEADBEEF
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	const_0x10001                   // -- Begin function const_0x10001
; CHECK: 	.type	const_0x10001,@function
; CHECK: const_0x10001:                          // @const_0x10001
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r1, r0, 65537 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	const_0x10001, .Lfunc_end12-const_0x10001
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	add_pow2_small                  // -- Begin function add_pow2_small
; CHECK: 	.type	add_pow2_small,@function
; CHECK: add_pow2_small:                         // @add_pow2_small
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ addi32	r1, r1, 4; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	add_pow2_small, .Lfunc_end13-add_pow2_small
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	add_pow2_large                  // -- Begin function add_pow2_large
; CHECK: 	.type	add_pow2_large,@function
; CHECK: add_pow2_large:                         // @add_pow2_large
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ nop; nop; addi32_w	r2, r0, 65536 }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	add_pow2_large, .Lfunc_end14-add_pow2_large
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	or_large_const                  // -- Begin function or_large_const
; CHECK: 	.type	or_large_const,@function
; CHECK: or_large_const:                         // @or_large_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 8; nop; nop }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ ori32	r1, r1, 65536; nop; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 8 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	or_large_const, .Lfunc_end15-or_large_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define i32 @const_pow2_2() {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


  ret i32 2
}

define i32 @const_pow2_0x100000() {
; const_pow2_0x100000:
; 0x100000 = 1<<20 -> LUI 1 + addi32{{(_w)?}} 0 (canonical LUI pair; bit20 set)
  ret i32 1048576
}

define i32 @const_0x80000000() {
; const_0x80000000:
; INT_MIN = 1<<31 -> single LUI 0x800
  ret i32 -2147483648
}

;================================================================
;THE BUG (ISA-43 #1): 0xF0000000 must load correctly.
;Pre-fix the backend emitted `lui 61440` (16-bit); hardware did
;(61440 & 0xFFF)<<20 = 0. Now it emits `lui 3840` (0xF00) -> 0xF0000000.
;(MatInt always emits the LUI+addi32{{(_w)?}} pair form when LUI is chosen.)
;================================================================

define i32 @const_0xF0000000() {
  ret i32 -268435456
}

;================================================================
;Upper-half-only values (low 16 bits zero). With simm20 range
;0xFFFF0000 = -65536 and 0x00010000 = +65536 are single ADDI32_W
;(the simm20 from R0 reconstructs the full 32-bit value).
;================================================================

define i32 @const_upper_0xFFFF0000() {
  ret i32 -65536
}

define i32 @const_upper_0x00010000() {
  ret i32 65536
}

;================================================================
;simm16 boundary -> single ADDI32_W
;================================================================

define i32 @const_simm16_max() {
  ret i32 32767
}

define i32 @const_simm16_min() {
  ret i32 -32768
}

;================================================================
;uimm16 (32768..65535) -> single ADDI32_W from R0
;(simm20 covers these unsigned-looking values; ORI32 is no longer used.)
;================================================================

define i32 @const_0x8000() {
  ret i32 32768
}

define i32 @const_0xFFFF() {
  ret i32 65535
}

;================================================================
;Full 32-bit -> `lui hi12` + `addi32{{(_w)?}} simm20` (2-instruction pair).
;Round-trips when (hi12<<20 + simm20) reconstructs V.
;================================================================

define i32 @const_0x12345678() {
  ret i32 305419896
}

define i32 @const_0xDEADBEEF() {
  ret i32 -559038737
}

define i32 @const_0x10001() {
; const_0x10001:
; 0x10001 fits in simm20 -> single addi32{{(_w)?}} from R0.
  ret i32 65537
}

;================================================================
;Constants used in arithmetic
;================================================================

define i32 @add_pow2_small(i32 %x) {
  %r = add i32 %x, 4
  ret i32 %r
}

define i32 @add_pow2_large(i32 %x) {
; add_pow2_large:
; 65536 fits in simm20 -> single addi32{{(_w)?}} from R0, then add32
; add32 verified at line 306 above.
  %r = add i32 %x, 65536
  ret i32 %r
}

define i32 @or_large_const(i32 %x) {
; or32 verified at line 326 above.
  %r = or i32 %x, 65536
  ret i32 %r
}
