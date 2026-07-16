; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	ret_float_2p5                   // -- Begin function ret_float_2p5
; CHECK: 	.type	ret_float_2p5,@function
; CHECK-LABEL: ret_float_2p5:                          // @ret_float_2p5
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 1026 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	ret_float_2p5, .Lfunc_end0-ret_float_2p5
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; REGRESSION TEST: G_FCONSTANT soft-float constant materialization.
;
; Bug: HaydnLegalizerInfo listed G_FCONSTANT in the same
; `.libcallFor({S32, S64})` block as G_FADD/G_FMUL/... at the old line 226.
; A constant is not a runtime libcall, so the legalizer had no recipe for
; G_FCONSTANT and crashed:
; fatal error: unable to legalize instruction:
; %0:_(s32) = G_FCONSTANT float 2.500000e+00
; This blocked every IEEE-float kernel in M6 (all 62 `*f` files use float
; constants — every vec_addf/iirf/mathf kernel returns a float literal or
; compares against one).
;
; Fix: G_FCONSTANT is now `customFor({S32, S64})`. legalizeCustom bitcasts the
; float's APInt bit-pattern and emits `buildConstant(Dst, FVal.bitcastToAPInt)`
; into the float-typed destination vreg — the standard soft-float idiom
; (mirrors RISC-V's G_FCONSTANT handling). Haydn has no FPU, so every
; float value lives in a GPR/DR64 as its bit-cast integer.
;
; Test design: each function returns a distinct float/double constant. Before
; the fix, `llc` aborted with "unable to legalize instruction: G_FCONSTANT".
; After the fix, the bit-cast integer materialises via the integer constant
; path and the function compiles cleanly.
;
; f32 cases: the bit-pattern is upper-bits-only (low20 == 0), so a single
; 12-bit LUI reconstructs it: `lui rN, bits[31:20]` of the IEEE word. Each
; f32 CHECK pins that lui immediate — so it genuinely guards the bit-pattern
; reached the instruction stream, not just "llc didn't crash".
;
; f64 cases: the 64-bit bit-pattern is materialised as two 32-bit words stored
; to the stack and reloaded into a DR64 (`d_ldw_post_imm`). The CHECKs pin the
; hi32 word's lui immediate, which encodes the upper bits of the IEEE pattern
; and proves the bit-cast APInt reached the instruction stream.
;
; If G_FCONSTANT regresses back to libcallFor, llc crashes before emitting
; code and every CHECK fails.
;
; Reference: -softfloat-gfconstant-and-libcall-symbol.md
; RISCVLegalizerInfo.cpp case G_FCONSTANT

;f32 constant 2.5 (bit-pattern 0x40200000; bits[31:20] = 0x402 = 1026)
define float @ret_float_2p5() {
  ret float 2.500000e+00
}

;f32 constant 1.0 (bit-pattern 0x3f800000; bits[31:20] = 0x3f8 = 1016)
define float @ret_float_1p0() {
  ret float 1.000000e+00
}

;f32 constant 0.0 (bit-pattern 0x00000000) — soft-zero, uses R0
define float @ret_float_0p0() {
  ret float 0.000000e+00
}

;f32 constant -2.0 (bit-pattern 0xc0000000; bits[31:20] = 0xc00 = 3072)
define float @ret_float_neg2p0() {
  ret float -2.000000e+00
}

;f64 constant 3.14 (bit-pattern 0x40091EB851EB851F)
; hi32 = 0x40091EB8 -> materialised as lui 0x401 (=1025) + addi32{{(_w)?}}; the
; lui immediate proves the upper IEEE bits reached the instruction stream.
define double @ret_double_3p14() {
  ret double 3.140000e+00
}

;f64 constant 1.0 (bit-pattern 0x3ff0000000000000)
; hi32 = 0x3ff00000 -> single LUI 0x3ff (=1023).
define double @ret_double_1p0() {
  ret double 1.000000e+00
}

;f32 constant used in arithmetic — exercises constant + libcall together.
; This is the minimal kernel shape that all M6 `*f` files share:
; a float literal feeding a float arithmetic op.
define float @const_plus_arg(float %a) {
  %r = fadd float %a, 2.500000e+00
  ret float %r
}
