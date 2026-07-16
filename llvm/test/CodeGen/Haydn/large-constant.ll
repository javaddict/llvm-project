; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs %s -o - | FileCheck %s

; CHECK: 	.globl	test_large_positive             // -- Begin function test_large_positive
; CHECK: 	.type	test_large_positive,@function
; CHECK-LABEL: test_large_positive:                    // @test_large_positive
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	addi32{{(_w)?}}	r1, r0, 65536 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_large_positive, .Lfunc_end0-test_large_positive
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

; Test large constant materialization (constants that don't fit in simm16).
; ISA-43 #1 / : LUI is 12-bit (sets bits[31:20]); the materialiser uses
; ORI32 from R0 for uimm16, ADDI32 for simm16, and the 2-instruction
; `lui hi12` + `addi32{{(_w)?}} simm20` pair for every other case (the 12+20 split
; covers the whole 32-bit space; canonical pair post-ASO-removal).
; Every sequence below verified by reconstructing the
; value from R0: hi12 = (V + 0x80000) >> 20, simm20 = V - (hi12 << 20).

define i32 @test_large_positive() {
; 0x10000: fits in simm20 -> single addi32{{(_w)?}} from R0.
  ret i32 65536
}

define i32 @test_large_negative() {
; 0xFFFF0000 = -65536: fits in simm20 -> single addi32{{(_w)?}} from R0.
  ret i32 -65536
}

define i32 @test_max_positive() {
; 0x7FFFFFFF: lui 0x800 (=0x80000000); addi32{{(_w)?}} -1 -> 0x7FFFFFFF.
  ret i32 2147483647
}

define i32 @test_min_negative() {
; 0x80000000: single LUI 0x800.
  ret i32 -2147483648
}

define i32 @test_bit_pattern() {
; 0x92461840: round-trips through (2340<<20 + 0x61840) -> 2-instruction pair.
  ret i32 2454067264
}

define i32 @test_small_positive() {
  ret i32 100
}

define i32 @test_small_negative() {
  ret i32 -100
}

define i32 @test_zero() {
  ret i32 0
}

define i32 @test_add_with_large() {
; 65536 + 42 = 65578 = 0x1002A: fits in simm20 -> single addi32{{(_w)?}} from R0.
  %1 = add i32 65536, 42
  ret i32 %1
}

define i32 @test_pointer_constant(i32 %a) {
; 0x10000 used in arithmetic: single addi32{{(_w)?}} from R0 (simm20), then add32.
  %2 = add i32 %a, 65536
  ret i32 %2
}
