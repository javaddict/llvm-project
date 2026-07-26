; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs %s -o - | FileCheck %s



; Test large constant materialization (constants that don't fit in simm16).
; ISA-43 #1 / : LUI is 12-bit (sets bits[31:20]); the materialiser uses
; ORI32 from R0 for uimm16, ADDI32 for simm16, and the 2-instruction
; `lui hi12` + `addi32{{(_w)?}} simm20` pair for every other case (the 12+20 split
; covers the whole 32-bit space; canonical pair post-ASO-removal).
; Every sequence below verified by reconstructing the
; value from R0: hi12 = (V + 0x80000) >> 20, simm20 = V - (hi12 << 20).

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_large_positive             // -- Begin function test_large_positive
; CHECK: 	.type	test_large_positive,@function
; CHECK: test_large_positive:                    // @test_large_positive
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 65536; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_large_positive, .Lfunc_end0-test_large_positive
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_large_negative             // -- Begin function test_large_negative
; CHECK: 	.type	test_large_negative,@function
; CHECK: test_large_negative:                    // @test_large_negative
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, -65536; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_large_negative, .Lfunc_end1-test_large_negative
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_max_positive               // -- Begin function test_max_positive
; CHECK: 	.type	test_max_positive,@function
; CHECK: test_max_positive:                      // @test_max_positive
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		lui	r1, 2048; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, -1; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_max_positive, .Lfunc_end2-test_max_positive
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_min_negative               // -- Begin function test_min_negative
; CHECK: 	.type	test_min_negative,@function
; CHECK: test_min_negative:                      // @test_min_negative
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		lui	r1, 2048; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_min_negative, .Lfunc_end3-test_min_negative
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_bit_pattern                // -- Begin function test_bit_pattern
; CHECK: 	.type	test_bit_pattern,@function
; CHECK: test_bit_pattern:                       // @test_bit_pattern
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		lui	r1, 2340; 	nop; 	nop }
; CHECK: 	{ 		addi32_w	r1, r1, 399424; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_bit_pattern, .Lfunc_end4-test_bit_pattern
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_small_positive             // -- Begin function test_small_positive
; CHECK: 	.type	test_small_positive,@function
; CHECK: test_small_positive:                    // @test_small_positive
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 100; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	test_small_positive, .Lfunc_end5-test_small_positive
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_small_negative             // -- Begin function test_small_negative
; CHECK: 	.type	test_small_negative,@function
; CHECK: test_small_negative:                    // @test_small_negative
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, -100; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	test_small_negative, .Lfunc_end6-test_small_negative
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_zero                       // -- Begin function test_zero
; CHECK: 	.type	test_zero,@function
; CHECK: test_zero:                              // @test_zero
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 0; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end7:
; CHECK: 	.size	test_zero, .Lfunc_end7-test_zero
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_add_with_large             // -- Begin function test_add_with_large
; CHECK: 	.type	test_add_with_large,@function
; CHECK: test_add_with_large:                    // @test_add_with_large
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r1, r0, 65578; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end8:
; CHECK: 	.size	test_add_with_large, .Lfunc_end8-test_add_with_large
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_pointer_constant           // -- Begin function test_pointer_constant
; CHECK: 	.type	test_pointer_constant,@function
; CHECK: test_pointer_constant:                  // @test_pointer_constant
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		nop; 	nop; 	subi32	sp, sp, 8 }
; CHECK: 	.cfi_def_cfa_offset 8
; CHECK: 	{ 		addi32_w	r2, r0, 65536; 	nop; 	nop }
; CHECK: 	{ 		nop; 	nop; 	add32	r1, r1, r2 }
; CHECK: 	{ 		nop; 	nop; 	xor32	r0, r0, r0 }
; CHECK: 	{ 		addi32_w	sp, sp, 8; 	nop; 	nop }
; CHECK: 	{ 		jalr_w	r0, lr, 0; 	nop; 	nop }
; CHECK: .Lfunc_end9:
; CHECK: 	.size	test_pointer_constant, .Lfunc_end9-test_pointer_constant
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

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
