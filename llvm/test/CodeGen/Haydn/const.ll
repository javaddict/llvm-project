; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	const_zero                      // -- Begin function const_zero
; CHECK: 	.type	const_zero,@function
; CHECK-LABEL: const_zero:                             // @const_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	const_zero, .Lfunc_end0-const_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function


define i32 @const_zero() {
  ret i32 0
}

define i32 @const_small() {
  ret i32 42
}

define i32 @const_large() {
  ret i32 12345678
}
