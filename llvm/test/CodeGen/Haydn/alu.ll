; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -haydn-enable-gformat-select=1 < %s | FileCheck %s

; CHECK: 	.globl	add                             // -- Begin function add
; CHECK: 	.type	add,@function
; CHECK-LABEL: add:                                    // @add
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	add, .Lfunc_end0-add
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function


define i32 @add(i32 %a, i32 %b) {
  %result = add i32 %a, %b
  ret i32 %result
}

define i32 @sub(i32 %a, i32 %b) {
  %result = sub i32 %a, %b
  ret i32 %result
}

define i32 @mul(i32 %a, i32 %b) {
  %result = mul i32 %a, %b
  ret i32 %result
}

define i32 @and_op(i32 %a, i32 %b) {
  %result = and i32 %a, %b
  ret i32 %result
}

define i32 @or_op(i32 %a, i32 %b) {
  %result = or i32 %a, %b
  ret i32 %result
}

define i32 @xor_op(i32 %a, i32 %b) {
  %result = xor i32 %a, %b
  ret i32 %result
}
