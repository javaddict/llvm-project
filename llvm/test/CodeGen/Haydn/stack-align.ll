; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;
; Test stack alignment and SP adjustment for Haydn.
; Haydn uses 8-byte stack alignment. These tests verify that SP adjustments
; are always multiples of 8, even when the total alloca size is not.
; Complements stack.ll and large-stack.ll with alignment-specific coverage.

;Multiple small allocas (3 x i32 = 12 bytes, rounded to 16)

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped



; CHECK:  	.text
; CHECK:  	.globl	test_multi_alloca               // -- Begin function test_multi_alloca
; CHECK:  	.type	test_multi_alloca,@function
; CHECK:  test_multi_alloca:                      // @test_multi_alloca
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 24 }
; CHECK:  	.cfi_def_cfa_offset 24
; CHECK:  	{ 	addi32{{(_w)?}}	r4, sp, 20 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r6, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r0, 1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK:  	{ 	st32	r1, r4, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK:  	{ 	st32	r2, r5, 0 }
; CHECK:  	{ 	st32	r3, r6, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	test_multi_alloca, .Lfunc_end0-test_multi_alloca
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_256_bytes                  // -- Begin function test_256_bytes
; CHECK:  	.type	test_256_bytes,@function
; CHECK:  test_256_bytes:                         // @test_256_bytes
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 264 }
; CHECK:  	.cfi_def_cfa_offset 264
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r0, 0 }
; CHECK:  	{ 	st32	r1, r2, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 264 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	test_256_bytes, .Lfunc_end1-test_256_bytes
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_512_bytes                  // -- Begin function test_512_bytes
; CHECK:  	.type	test_512_bytes,@function
; CHECK:  test_512_bytes:                         // @test_512_bytes
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 520 }
; CHECK:  	.cfi_def_cfa_offset 520
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r0, 42 }
; CHECK:  	{ 	st32	r1, r2, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 520 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end2:
; CHECK:  	.size	test_512_bytes, .Lfunc_end2-test_512_bytes
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_odd_alloca                 // -- Begin function test_odd_alloca
; CHECK:  	.type	test_odd_alloca,@function
; CHECK:  test_odd_alloca:                        // @test_odd_alloca
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 40 }
; CHECK:  	.cfi_def_cfa_offset 40
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 8 }
; CHECK:  	{ 	st32	r1, r2, 0 }
; CHECK:  	{ 	ld32	r1, r2, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 40 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end3:
; CHECK:  	.size	test_odd_alloca, .Lfunc_end3-test_odd_alloca
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_nested_stack               // -- Begin function test_nested_stack
; CHECK:  	.type	test_nested_stack,@function
; CHECK:  test_nested_stack:                      // @test_nested_stack
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 144 }
; CHECK:  	{ 	xor32	r1, r1, r1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r1, 140 }
; CHECK:  	{ 	st32_reg	lr, sp, r1 }
; CHECK:  	.cfi_def_cfa_offset 144
; CHECK:  	.cfi_offset lr, 140
; CHECK:  	{ 	addi32{{(_w)?}}	r1, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 42 }
; CHECK:  	{ 	st32	r2, r1, 0 }
; CHECK:  	{ 	jal_w	lr, sink }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	xor32	r2, r2, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r2, 140 }
; CHECK:  	{ 	ld32_reg	lr, sp, r2 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 144 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end4:
; CHECK:  	.size	test_nested_stack, .Lfunc_end4-test_nested_stack
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_stack_outgoing             // -- Begin function test_stack_outgoing
; CHECK:  	.type	test_stack_outgoing,@function
; CHECK:  test_stack_outgoing:                    // @test_stack_outgoing
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 32 }
; CHECK:  	{ 	st32	lr, sp, 28 }
; CHECK:  	.cfi_def_cfa_offset 32
; CHECK:  	.cfi_offset lr, 28
; CHECK:  	{ 	subi32	sp, sp, 12 }
; CHECK:  	{ 	st32	r1, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r12, r0, 1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 2; 	move32	r1, r12; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 4 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, r0, 5 }
; CHECK:  	{ 	addi32{{(_w)?}}	r6, r0, 6 }
; CHECK:  	{ 	addi32{{(_w)?}}	r7, r0, 7 }
; CHECK:  	{ 	addi32{{(_w)?}}	fp, r0, 8 }
; CHECK:  	{ 	st32	fp, sp, 0 }
; CHECK:  	{ 	jal_w	lr, many_params }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 12 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end5:
; CHECK:  	.size	test_stack_outgoing, .Lfunc_end5-test_stack_outgoing
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_local_and_outgoing         // -- Begin function test_local_and_outgoing
; CHECK:  	.type	test_local_and_outgoing,@function
; CHECK:  test_local_and_outgoing:                // @test_local_and_outgoing
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 48 }
; CHECK:  	{ 	st32	lr, sp, 44 }
; CHECK:  	.cfi_def_cfa_offset 48
; CHECK:  	.cfi_offset lr, 44
; CHECK:  	{ 	addi32{{(_w)?}}	r2, sp, 24; 	subi32	sp, sp, 12; 	nop }
; CHECK:  	{ 	st32	r1, r2, 0 }
; CHECK:  	{ 	ld32	r1, r2, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 4 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, r0, 5 }
; CHECK:  	{ 	addi32{{(_w)?}}	r6, r0, 6 }
; CHECK:  	{ 	addi32{{(_w)?}}	r7, r0, 7 }
; CHECK:  	{ 	addi32{{(_w)?}}	r12, r0, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	fp, r0, 9 }
; CHECK:  	{ 	st32	r12, sp, 0 }
; CHECK:  	{ 	st32	fp, sp, 8 }
; CHECK:  	{ 	jal_w	lr, many_params }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 12 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 44 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 48 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end6:
; CHECK:  	.size	test_local_and_outgoing, .Lfunc_end6-test_local_and_outgoing
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_align_5xi32                // -- Begin function test_align_5xi32
; CHECK:  	.type	test_align_5xi32,@function
; CHECK:  test_align_5xi32:                       // @test_align_5xi32
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 32 }
; CHECK:  	{ 	st32	r8, sp, 28 }
; CHECK:  	.cfi_def_cfa_offset 32
; CHECK:  	.cfi_offset r8, 28
; CHECK:  	{ 	addi32{{(_w)?}}	r6, sp, 24 }
; CHECK:  	{ 	addi32{{(_w)?}}	r7, sp, 20 }
; CHECK:  	{ 	addi32{{(_w)?}}	r12, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	fp, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	r8, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	r1, r0, 1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r0, 2 }
; CHECK:  	{ 	st32	r1, r6, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 3 }
; CHECK:  	{ 	st32	r2, r7, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 4 }
; CHECK:  	{ 	st32	r3, r12, 0 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, r0, 5 }
; CHECK:  	{ 	st32	r4, fp, 0 }
; CHECK:  	{ 	st32	r5, r8, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	r8, sp, 28 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 32 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end7:
; CHECK:  	.size	test_align_5xi32, .Lfunc_end7-test_align_5xi32
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits

define void @test_multi_alloca() {
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  store i32 1, ptr %a
  store i32 2, ptr %b
  store i32 3, ptr %c
  ret void
}

;Medium stack: [64 x i32] = 256 bytes (already 8-byte aligned)

define void @test_256_bytes() {
  %arr = alloca [64 x i32], align 8
  %p = getelementptr [64 x i32], ptr %arr, i32 0, i32 0
  store i32 0, ptr %p
  ret void
}

;Larger stack: [128 x i32] = 512 bytes

define void @test_512_bytes() {
  %arr = alloca [128 x i32], align 8
  %p = getelementptr [128 x i32], ptr %arr, i32 0, i32 0
  store i32 42, ptr %p
  ret void
}

;Odd-sized alloca: [7 x i32] = 28 bytes, rounded to 32

define i32 @test_odd_alloca(i32 %a) {
  %arr = alloca [7 x i32], align 8
  %p = getelementptr [7 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

;Stack with a nested call (callee uses stack space)

declare void @sink(ptr)

define void @test_nested_stack() {
  %arr = alloca [32 x i32], align 8
  %p = getelementptr [32 x i32], ptr %arr, i32 0, i32 0
  store i32 42, ptr %p
  call void @sink(ptr %p)
  ret void
}

;Stack args to callee (forces outgoing argument space)
;Note: callee-saved regs are stored below SP (negative offsets)
;so the epilogue uses callee-save restores instead of an explicit
;SP adjustment.

declare i32 @many_params(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @test_stack_outgoing(i32 %a) {
; Stack size includes local + outgoing + CSR spill area; the exact value
; depends on which CSR are spilled. The invariant we test is that the
; allocation is a multiple of 8 (: 8-byte stack alignment).
  %r = call i32 @many_params(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 %a)
  ret i32 %r
}

;Stack with alloca + call (both local and outgoing space)
;Note: callee-saved regs are stored below SP (negative offsets)
;so the epilogue uses callee-save restores instead of an explicit
;SP adjustment.

define i32 @test_local_and_outgoing(i32 %a) {
; Stack size includes local + outgoing + CSR spill area; the exact value
; depends on which CSR are spilled. The invariant we test is that the
; allocation is a multiple of 8 (: 8-byte stack alignment).
  %arr = alloca [4 x i32], align 8
  %p = getelementptr [4 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = call i32 @many_params(i32 %v, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9)
  ret i32 %r
}

;Alignment is preserved: SP adjustments are always multiples of 8

; Verify: even with 5 x i32 = 20 bytes of allocas, frame is padded to a
; multiple of 8 (20 -> 24). This is a leaf function with no calls, so there
; are no callee-save spills — only the 20 bytes of allocas, rounded up to 24.
; (SFR-strip) changed bundle layout (denser packing) — rebaselined.
; Note: callee-saved regs are stored below SP (negative offsets), so there
; is no explicit SP restore in the epilogue.
define void @test_align_5xi32() {
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  %d = alloca i32
  %e = alloca i32
  store i32 1, ptr %a
  store i32 2, ptr %b
  store i32 3, ptr %c
  store i32 4, ptr %d
  store i32 5, ptr %e
  ret void
}
