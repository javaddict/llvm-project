; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.file	"<stdin>"
; CHECK: 	.text
; CHECK: 	.globl	must_zero_r0                    // -- Begin function must_zero_r0
; CHECK: 	.type	must_zero_r0,@function
; CHECK-LABEL: must_zero_r0:                           // @must_zero_r0
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	must_zero_r0, .Lfunc_end0-must_zero_r0
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

;
; REGRESSION TEST: R0 must be zeroed at function entry unconditionally.
;
; Bug: the R0-zeroing in emitPrologue was guarded by
; `!MBBI->isImplicitDef`, so when the entry block began with an implicit-def
; (common after fast-isel/regalloc), R0 was never zeroed. R0 is the soft-zero
; register used by ADDI32, copyPhysReg, and constant materialization — leaving
; it uninitialized corrupts every zero-based computation.
; Fix: drop the guard; always emit ZERO_GPR R0 at entry. See F31 / CLAUDE.md
; register map (R0=soft-zero).
;
; Test design: any function must emit xor32 r0, r0, r0 as the first prologue
; instruction. The CHECK below verifies the zeroing is always present.

define i32 @must_zero_r0(i32 %a) {
  %sum = add i32 %a, 0
  ret i32 %sum
}
