; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Test constant materialization strategies for Haydn.
; Covers small immediates (single ADDI32), medium (LUI+ADDI32 with sign-extension
; compensation), and special values (0, -1, INT_MIN, INT_MAX).
; Complements const.ll and large-imm.ll with patterns not covered there.

;Zero and trivial constants

define i32 @const_zero() {
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
  ret i32 0
}

define i32 @const_one() {
; CHECK: 	.globl	const_one                       // -- Begin function const_one
; CHECK: 	.type	const_one,@function
; CHECK-LABEL: const_one:                              // @const_one
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	const_one, .Lfunc_end1-const_one
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 1
}

define i32 @const_neg_one() {
; CHECK: 	.globl	const_neg_one                   // -- Begin function const_neg_one
; CHECK: 	.type	const_neg_one,@function
; CHECK-LABEL: const_neg_one:                          // @const_neg_one
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	const_neg_one, .Lfunc_end2-const_neg_one
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -1
}

;Small positive constants (fit in simm16, single ADDI32)

define i32 @const_small_pos() {
; CHECK: 	.globl	const_small_pos                 // -- Begin function const_small_pos
; CHECK: 	.type	const_small_pos,@function
; CHECK-LABEL: const_small_pos:                        // @const_small_pos
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 42 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	const_small_pos, .Lfunc_end3-const_small_pos
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 42
}

define i32 @const_255() {
; CHECK: 	.globl	const_255                       // -- Begin function const_255
; CHECK: 	.type	const_255,@function
; CHECK-LABEL: const_255:                              // @const_255
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 255 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	const_255, .Lfunc_end4-const_255
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 255
}

define i32 @const_max_simm16() {
; CHECK: 	.globl	const_max_simm16                // -- Begin function const_max_simm16
; CHECK: 	.type	const_max_simm16,@function
; CHECK-LABEL: const_max_simm16:                       // @const_max_simm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 32767 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	const_max_simm16, .Lfunc_end5-const_max_simm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: lui
  ret i32 32767
}

;Small negative constants (fit in simm16, single ADDI32)

define i32 @const_small_neg() {
; CHECK: 	.globl	const_small_neg                 // -- Begin function const_small_neg
; CHECK: 	.type	const_small_neg,@function
; CHECK-LABEL: const_small_neg:                        // @const_small_neg
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -42 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	const_small_neg, .Lfunc_end6-const_small_neg
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -42
}

define i32 @const_min_simm16() {
; CHECK: 	.globl	const_min_simm16                // -- Begin function const_min_simm16
; CHECK: 	.type	const_min_simm16,@function
; CHECK-LABEL: const_min_simm16:                       // @const_min_simm16
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -32768 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	const_min_simm16, .Lfunc_end7-const_min_simm16
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -32768
}

;Large constants requiring LUI+ADDI32

; 0xFFFF = 65535: fits in simm20 (single-instruction MatInt form)
define i32 @const_0xFFFF() {
; CHECK: 	.globl	const_0xFFFF                    // -- Begin function const_0xFFFF
; CHECK: 	.type	const_0xFFFF,@function
; CHECK-LABEL: const_0xFFFF:                           // @const_0xFFFF
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 65535 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	const_0xFFFF, .Lfunc_end8-const_0xFFFF
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 65535
}

; 0xFFFFFFFF = -1 = UINT_MAX: single ADDI32_W -1
define i32 @const_all_ones() {
; CHECK: 	.globl	const_all_ones                  // -- Begin function const_all_ones
; CHECK: 	.type	const_all_ones,@function
; CHECK-LABEL: const_all_ones:                         // @const_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	const_all_ones, .Lfunc_end9-const_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -1
}

; 0x80000000 = INT_MIN: LUI 0x800 + addi32{{(_w)?}} 0 (12-bit LUI sets bits[31:20]). ISA-43 #1.
define i32 @const_int_min() {
; CHECK: 	.globl	const_int_min                   // -- Begin function const_int_min
; CHECK: 	.type	const_int_min,@function
; CHECK-LABEL: const_int_min:                          // @const_int_min
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 2048 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end10:
; CHECK: 	.size	const_int_min, .Lfunc_end10-const_int_min
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -2147483648
}

; 0x7FFFFFFF = INT_MAX: LUI 0x800 + ADDI32_W -1 (12-bit LUI; addi closes the gap)
define i32 @const_int_max() {
; CHECK: 	.globl	const_int_max                   // -- Begin function const_int_max
; CHECK: 	.type	const_int_max,@function
; CHECK-LABEL: const_int_max:                          // @const_int_max
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 2048 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, -1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end11:
; CHECK: 	.size	const_int_max, .Lfunc_end11-const_int_max
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 2147483647
}

; 0xDEADBEEF: round-trips -> LUI 0xDDB + addi32{{(_w)?}} (2-instruction pair)
define i32 @const_deadbeef() {
; CHECK: 	.globl	const_deadbeef                  // -- Begin function const_deadbeef
; CHECK: 	.type	const_deadbeef,@function
; CHECK-LABEL: const_deadbeef:                         // @const_deadbeef
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 3563 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, -147729 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end12:
; CHECK: 	.size	const_deadbeef, .Lfunc_end12-const_deadbeef
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 -559038737
}

; 0x12345678: round-trips -> LUI 0x123 + addi32{{(_w)?}} (2-instruction pair)
define i32 @const_12345678() {
; CHECK: 	.globl	const_12345678                  // -- Begin function const_12345678
; CHECK: 	.type	const_12345678,@function
; CHECK-LABEL: const_12345678:                         // @const_12345678
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	lui	r1, 291 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r1, 284280 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end13:
; CHECK: 	.size	const_12345678, .Lfunc_end13-const_12345678
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  ret i32 305419896
}

; 0x10000: fits in simm20 (single ADDI32_W from R0; 12+20 split no longer needed)
define i32 @const_0x10000() {
; CHECK: 	.globl	const_0x10000                   // -- Begin function const_0x10000
; CHECK: 	.type	const_0x10000,@function
; CHECK-LABEL: const_0x10000:                          // @const_0x10000
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 65536 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end14:
; CHECK: 	.size	const_0x10000, .Lfunc_end14-const_0x10000
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: lui
; CHECK-NOT: slli32
  ret i32 65536
}

;Constants used in arithmetic (verifying correct propagation)

; x + 1 should use ADDI32_W immediate form
define i32 @add_one(i32 %x) {
; CHECK: 	.globl	add_one                         // -- Begin function add_one
; CHECK: 	.type	add_one,@function
; CHECK-LABEL: add_one:                                // @add_one
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end15:
; CHECK: 	.size	add_one, .Lfunc_end15-add_one
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  %r = add i32 %x, 1
  ret i32 %r
}

; x - 1 via sub
define i32 @sub_one(i32 %x) {
; CHECK: 	.globl	sub_one                         // -- Begin function sub_one
; CHECK: 	.type	sub_one,@function
; CHECK-LABEL: sub_one:                                // @sub_one
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r2, r0, 1 }
; CHECK: 	{ 	sub32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end16:
; CHECK: 	.size	sub_one, .Lfunc_end16-sub_one
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  %r = sub i32 %x, 1
  ret i32 %r
}

; x | -1 is simplified to -1 by the combiner (all bits set)
define i32 @or_all_ones(i32 %x) {
; CHECK: 	.globl	or_all_ones                     // -- Begin function or_all_ones
; CHECK: 	.type	or_all_ones,@function
; CHECK-LABEL: or_all_ones:                            // @or_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, -1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end17:
; CHECK: 	.size	or_all_ones, .Lfunc_end17-or_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK-NOT: or32
  %r = or i32 %x, -1
  ret i32 %r
}

; x ^ -1 (bitwise NOT) → NOT32 (GISel Pat: not / xor-all-ones)
define i32 @xor_all_ones(i32 %x) {
; CHECK: 	.globl	xor_all_ones                    // -- Begin function xor_all_ones
; CHECK: 	.type	xor_all_ones,@function
; CHECK-LABEL: xor_all_ones:                           // @xor_all_ones
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	not32	r1, r1 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end18:
; CHECK: 	.size	xor_all_ones, .Lfunc_end18-xor_all_ones
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
  %r = xor i32 %x, -1
  ret i32 %r
}

; x & -1 is simplified to x by the combiner (identity)
define i32 @and_all_ones(i32 %x) {
; CHECK-LABEL: and_all_ones:
; CHECK-NOT: and32
  %r = and i32 %x, -1
  ret i32 %r
}

; x * 0 (constant folded to 0, no mul32 emitted)
define i32 @mul_zero(i32 %x) {
; CHECK-LABEL: mul_zero:
; CHECK-NOT: mul32
  %r = mul i32 %x, 0
  ret i32 %r
}
