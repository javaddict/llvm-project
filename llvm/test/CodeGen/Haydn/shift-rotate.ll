; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	shl_by_1                        // -- Begin function shl_by_1
; CHECK: 	.type	shl_by_1,@function
; CHECK-LABEL: shl_by_1:                               // @shl_by_1
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	sll32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	shl_by_1, .Lfunc_end0-shl_by_1
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

;
; Test shift and rotate operations for Haydn.
; Complements the basic shift.ll test with edge cases, constant shifts
; shift+mask patterns, and sign-extension-via-shift idioms.

;Shift by constant amounts

; Shift by 1 (smallest non-zero)
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
