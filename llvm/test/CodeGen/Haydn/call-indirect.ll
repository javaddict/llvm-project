; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

;
; Test indirect function calls through function pointers.
; Covers: basic indirect call, function pointer as argument
; void function pointers, chained indirect calls, and
; calls through arrays of function pointers.
;
; Indirect calls (callee is a register) lower to JALR (jump-and-link-register):
; jalr_w lr, <fnptr>, 0
; Direct calls (callee is a symbol) lower to JAL (jump-and-link):
; jal_w lr, <symbol>
; (: indirect calls previously routed through JAL with a register callee
; which JAL's calltarget operand cannot hold -> the printer emitted a blank
; target `jal_w lr,` and the ISS decoded the missing target as r0=0 -> self-loop.
; Fixed by routing reg callees to JALR. See / HaydnCallLowering.)

;Basic indirect call: call via ptr argument

; REBASELINED (auto) dual-sched pre-RA order rebaseline;.file skipped



; CHECK:  	.text
; CHECK:  	.globl	test_basic_indirect             // -- Begin function test_basic_indirect
; CHECK:  	.type	test_basic_indirect,@function
; CHECK:  test_basic_indirect:                    // @test_basic_indirect
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	st32	lr, sp, 12 }
; CHECK:  	.cfi_def_cfa_offset 16
; CHECK:  	.cfi_offset lr, 12
; CHECK:  	{ 	move32	r3, r1; 	move32	r1, r2; 	nop }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r3, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end0:
; CHECK:  	.size	test_basic_indirect, .Lfunc_end0-test_basic_indirect
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_indirect_2arg              // -- Begin function test_indirect_2arg
; CHECK:  	.type	test_indirect_2arg,@function
; CHECK:  test_indirect_2arg:                     // @test_indirect_2arg
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	st32	lr, sp, 12 }
; CHECK:  	.cfi_def_cfa_offset 16
; CHECK:  	.cfi_offset lr, 12
; CHECK:  	{ 	move32	r4, r1; 	move32	r1, r2; 	nop }
; CHECK:  	{ 	move32	r2, r3 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r4, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end1:
; CHECK:  	.size	test_indirect_2arg, .Lfunc_end1-test_indirect_2arg
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_void_fp                    // -- Begin function test_void_fp
; CHECK:  	.type	test_void_fp,@function
; CHECK:  test_void_fp:                           // @test_void_fp
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	st32	lr, sp, 12 }
; CHECK:  	.cfi_def_cfa_offset 16
; CHECK:  	.cfi_offset lr, 12
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r1, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end2:
; CHECK:  	.size	test_void_fp, .Lfunc_end2-test_void_fp
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_chained_indirect           // -- Begin function test_chained_indirect
; CHECK:  	.type	test_chained_indirect,@function
; CHECK:  test_chained_indirect:                  // @test_chained_indirect
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, sp, 8 }
; CHECK:  	{ 	st32	lr, r3, 0 }
; CHECK:  	{ 	st32	r8, r3, 4 }
; CHECK:  	.cfi_def_cfa_offset 16
; CHECK:  	.cfi_offset r8, 12
; CHECK:  	.cfi_offset lr, 8
; CHECK:  	{ 	move32	r8, r1; 	move32	r1, r2; 	nop }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r8, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r8, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 8 }
; CHECK:  	{ 	ld32	r8, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end3:
; CHECK:  	.size	test_chained_indirect, .Lfunc_end3-test_chained_indirect
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_fp_from_array              // -- Begin function test_fp_from_array
; CHECK:  	.type	test_fp_from_array,@function
; CHECK:  test_fp_from_array:                     // @test_fp_from_array
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 16 }
; CHECK:  	{ 	st32	lr, sp, 12 }
; CHECK:  	.cfi_def_cfa_offset 16
; CHECK:  	.cfi_offset lr, 12
; CHECK:  	{ 	lui	r2, funcs }
; CHECK:  	{ 	addi32{{(_w)?}}	r2, r2, funcs }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 2 }
; CHECK:  	{ 	sll32	r3, r1, r3 }
; CHECK:  	{ 	add32	r2, r2, r3 }
; CHECK:  	{ 	ld32	r2, r2, 0 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r2, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end4:
; CHECK:  	.size	test_fp_from_array, .Lfunc_end4-test_fp_from_array
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_indirect_many_args         // -- Begin function test_indirect_many_args
; CHECK:  	.type	test_indirect_many_args,@function
; CHECK:  test_indirect_many_args:                // @test_indirect_many_args
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 40 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, sp, 28 }
; CHECK:  	{ 	st32	lr, r3, 0 }
; CHECK:  	{ 	st32	r9, r3, 4 }
; CHECK:  	{ 	st32	r8, r3, 8 }
; CHECK:  	.cfi_def_cfa_offset 40
; CHECK:  	.cfi_offset r8, 36
; CHECK:  	.cfi_offset r9, 32
; CHECK:  	.cfi_offset lr, 28
; CHECK:  	{ 	subi32	sp, sp, 16; 	move32	r8, r1; 	nop }
; CHECK:  	{ 	st32	r2, sp, 8 }
; CHECK:  	{ 	addi32{{(_w)?}}	fp, r0, 1 }
; CHECK:  	{ 	addi32{{(_w)?}}	r12, r0, 2; 	move32	r1, fp; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, r0, 3; 	move32	r2, r12; 	nop }
; CHECK:  	{ 	addi32{{(_w)?}}	r4, r0, 4 }
; CHECK:  	{ 	addi32{{(_w)?}}	r5, r0, 5 }
; CHECK:  	{ 	addi32{{(_w)?}}	r6, r0, 6 }
; CHECK:  	{ 	addi32{{(_w)?}}	r7, r0, 7 }
; CHECK:  	{ 	addi32{{(_w)?}}	r9, r0, 8 }
; CHECK:  	{ 	st32	r9, sp, 0 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r8, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 16 }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 28 }
; CHECK:  	{ 	ld32	r9, sp, 32 }
; CHECK:  	{ 	ld32	r8, sp, 36 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 40 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end5:
; CHECK:  	.size	test_indirect_many_args, .Lfunc_end5-test_indirect_many_args
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.globl	test_mixed_calls                // -- Begin function test_mixed_calls
; CHECK:  	.type	test_mixed_calls,@function
; CHECK:  test_mixed_calls:                       // @test_mixed_calls
; CHECK:  	.cfi_startproc
; CHECK:  // %bb.0:
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	subi32	sp, sp, 24 }
; CHECK:  	{ 	addi32{{(_w)?}}	r3, sp, 12 }
; CHECK:  	{ 	st32	lr, r3, 0 }
; CHECK:  	{ 	st32	r9, r3, 4 }
; CHECK:  	{ 	st32	r8, r3, 8 }
; CHECK:  	.cfi_def_cfa_offset 24
; CHECK:  	.cfi_offset r8, 20
; CHECK:  	.cfi_offset r9, 16
; CHECK:  	.cfi_offset lr, 12
; CHECK:  	{ 	move32	r8, r1; 	move32	r1, r2; 	nop }
; CHECK:  	{ 	jal_w{{(\.s[012])?}}	lr, direct_callee }
; CHECK:  	{ 	xor32	r0, r0, r0; 	move32	r9, r1; 	nop }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	lr, r8, 0 }
; CHECK:  	{ 	xor32	r0, r0, r0; 	add32	r1, r9, r1; 	nop }
; CHECK:  	{ 	xor32	r0, r0, r0 }
; CHECK:  	{ 	ld32	lr, sp, 12 }
; CHECK:  	{ 	ld32	r9, sp, 16 }
; CHECK:  	{ 	ld32	r8, sp, 20 }
; CHECK:  	{ 	addi32{{(_w)?}}	sp, sp, 24 }
; CHECK:  	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK:  .Lfunc_end6:
; CHECK:  	.size	test_mixed_calls, .Lfunc_end6-test_mixed_calls
; CHECK:  	.cfi_endproc
; CHECK:                                          // -- End function
; CHECK:  	.section	".note.GNU-stack","",@progbits


define i32 @test_basic_indirect(ptr %fp, i32 %x) {
  %r = call i32 %fp(i32 %x)
  ret i32 %r
}

;Indirect call with two arguments

define i32 @test_indirect_2arg(ptr %fp, i32 %a, i32 %b) {
  %r = call i32 %fp(i32 %a, i32 %b)
  ret i32 %r
}

;Void function pointer call

define void @test_void_fp(ptr %fp) {
  call void %fp()
  ret void
}

;Chained indirect calls (same fp called twice)

define i32 @test_chained_indirect(ptr %fp, i32 %x) {
  %r1 = call i32 %fp(i32 %x)
  %r2 = call i32 %fp(i32 %r1)
  ret i32 %r2
}

;Function pointer loaded from global array

@funcs = external global [4 x ptr]

define i32 @test_fp_from_array(i32 %idx) {
  %p = getelementptr [4 x ptr], ptr @funcs, i32 0, i32 %idx
  %fp = load ptr, ptr %p
  %r = call i32 %fp(i32 %idx)
  ret i32 %r
}

;Indirect call with many arguments (forces stack spilling)

define i32 @test_indirect_many_args(ptr %fp, i32 %a) {
  %r = call i32 %fp(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 %a)
  ret i32 %r
}

;Mix of direct and indirect calls in the same function

declare i32 @direct_callee(i32)

define i32 @test_mixed_calls(ptr %fp, i32 %x) {
  %r1 = call i32 @direct_callee(i32 %x)
  %r2 = call i32 %fp(i32 %r1)
  %result = add i32 %r1, %r2
  ret i32 %result
}
