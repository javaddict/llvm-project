; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: LUI + ADDI32_W universal 2-instruction constant
; materialisation (/ ISA-44 Branch A).
;
; ADDI32_W is the 48-bit Wide-imm ALU variant of ADDI32 (encoding_manual.md
; section 5, Class 000, opcode 0x08). It carries a 20-bit SIGNED immediate:
; rt = rs + sext(imm20)
; With the 12-bit LUI (imm12 into bits[31:20]) the two fields are contiguous
; (12 high + 20 low = 32), so every 32-bit value materialises in exactly 2
; instructions when no shorter candidate wins:
; hi12 = ((V + 0x80000) >> 20) & 0xFFF
; lo20 = V - (hi12 << 20); fits signed-20, carry absorbed
; lui rN, hi12
; addi32{{(_w)?}} rN, rN, lo20
;
; This replaces the 3-instruction ADDI32/SLLI32/ORI32 fallback for the
; bits[19:16] "hole" that the 12-bit-LUI + simm16-ADDI32 pair cannot reach
; (lo20 outside [-32768, 32767]). Constants that already materialise in 1
; instruction (small simm16 via ADDI32, uimm16 via ORI32, or LUI-only) are
; NOT regressed -- LUI+ADDI32_W only wins when it is strictly shorter than
; every other candidate.
;
; ADDI32_W's.td operands are (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm)
; the $rt=$rs tie was removed, so the asm is the 3-operand form:
; addi32{{(_w)?}} rN, rN, lo20

;0x12345678 -- lo20 = 0x45678 = 284280 (positive, outside simm16).
define i32 @const_0x12345678() {
; CHECK: 	.globl	const_0x12345678                // -- Begin function const_0x12345678
; CHECK: 	.type	const_0x12345678,@function
; CHECK-LABEL: const_0x12345678:                       // @const_0x12345678
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 291 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 284280 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	const_0x12345678, .Lfunc_end0-const_0x12345678
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: slli32
; CHECK-NOT: ori32
  ret i32 305419896
}

;0x12348000 -- lo20 = 0x48000 = 294912 (positive, exercises +0x80000 rounding).
define i32 @const_0x12348000() {
; CHECK: 	.globl	const_0x12348000                // -- Begin function const_0x12348000
; CHECK: 	.type	const_0x12348000,@function
; CHECK-LABEL: const_0x12348000:                       // @const_0x12348000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 291 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 294912 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	const_0x12348000, .Lfunc_end1-const_0x12348000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 305430528
}

;0x123B8000 -- lo20 = -0x48000 = -294912 (negative, outside simm16
;exercises ADDI32_W sign-extension of bit 19).
define i32 @const_0x123B8000() {
; CHECK: 	.globl	const_0x123B8000                // -- Begin function const_0x123B8000
; CHECK: 	.type	const_0x123B8000,@function
; CHECK-LABEL: const_0x123B8000:                       // @const_0x123B8000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 292 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, -294912 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	const_0x123B8000, .Lfunc_end2-const_0x123B8000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 305889280
}

;0xDEADBEEF -- classic large pattern, lo20 = -0x24111 = -147729 (negative).
define i32 @const_0xDEADBEEF() {
; CHECK: 	.globl	const_0xDEADBEEF                // -- Begin function const_0xDEADBEEF
; CHECK: 	.type	const_0xDEADBEEF,@function
; CHECK-LABEL: const_0xDEADBEEF:                       // @const_0xDEADBEEF
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 3563 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, -147729 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	const_0xDEADBEEF, .Lfunc_end3-const_0xDEADBEEF
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -559038737
}

;Regression guards: with the WIDE-only MatInt rewrite (+), simm16/uimm16
;values now materialise via a single ADDI32_W from R0 (simm20 covers both
;ranges; ORI32 is no longer used). These stay 1-instruction (no LUI pair).

define i32 @regression_simm16() {
; CHECK: 	.globl	regression_simm16               // -- Begin function regression_simm16
; CHECK: 	.type	regression_simm16,@function
; CHECK-LABEL: regression_simm16:                      // @regression_simm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 32767 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	regression_simm16, .Lfunc_end4-regression_simm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: lui r{{[0-9]+}},
  ret i32 32767
}

define i32 @regression_uimm16() {
; CHECK: 	.globl	regression_uimm16               // -- Begin function regression_uimm16
; CHECK: 	.type	regression_uimm16,@function
; CHECK-LABEL: regression_uimm16:                      // @regression_uimm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 65535 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	regression_uimm16, .Lfunc_end5-regression_uimm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: lui r{{[0-9]+}},
  ret i32 65535
}

define i32 @regression_lui_only() {
; CHECK: 	.globl	regression_lui_only             // -- Begin function regression_lui_only
; CHECK: 	.type	regression_lui_only,@function
; CHECK-LABEL: regression_lui_only:                    // @regression_lui_only
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 2048 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	regression_lui_only, .Lfunc_end6-regression_lui_only
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -2147483648
}

;The materialised hole constant feeds an add32 (the value is live, not DCE'd).
define i32 @add_with_hole_const(i32 %x) {
; CHECK: 	.globl	add_with_hole_const             // -- Begin function add_with_hole_const
; CHECK: 	.type	add_with_hole_const,@function
; CHECK-LABEL: add_with_hole_const:                    // @add_with_hole_const
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r2, 291 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r2, 284280 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	add_with_hole_const, .Lfunc_end7-add_with_hole_const
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  %r = add i32 %x, 305419896
  ret i32 %r
}
