; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Indirect calls lower to jalr.

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_basic_indirect             // -- Begin function test_basic_indirect
; CHECK: 	.type	test_basic_indirect,@function
; CHECK: test_basic_indirect:                    // @test_basic_indirect
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ move32	r3, r1; move32	r1, r2; nop }
; CHECK: 	{ nop; nop; jalr	lr, r3, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_basic_indirect, .Lfunc_end0-test_basic_indirect
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_indirect_2arg              // -- Begin function test_indirect_2arg
; CHECK: 	.type	test_indirect_2arg,@function
; CHECK: test_indirect_2arg:                     // @test_indirect_2arg
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ move32	r4, r1; move32	r1, r2; nop }
; CHECK: 	{ move32	r2, r3; nop; nop }
; CHECK: 	{ nop; nop; jalr	lr, r4, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_indirect_2arg, .Lfunc_end1-test_indirect_2arg
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_void_fp                    // -- Begin function test_void_fp
; CHECK: 	.type	test_void_fp,@function
; CHECK: test_void_fp:                           // @test_void_fp
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jalr	lr, r1, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end2:
; CHECK: 	.size	test_void_fp, .Lfunc_end2-test_void_fp
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_chained_indirect           // -- Begin function test_chained_indirect
; CHECK: 	.type	test_chained_indirect,@function
; CHECK: test_chained_indirect:                  // @test_chained_indirect
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	r3, sp, 8 }
; CHECK: 	{ nop; nop; st32	lr, r3, 0 }
; CHECK: 	{ nop; nop; st32	r8, r3, 4 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset r8, 12
; CHECK: 	.cfi_offset lr, 8
; CHECK: 	{ move32	r8, r1; move32	r1, r2; nop }
; CHECK: 	{ nop; nop; jalr	lr, r8, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; nop; jalr	lr, r8, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 8; nop }
; CHECK: 	{ nop; ld32	r8, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end3:
; CHECK: 	.size	test_chained_indirect, .Lfunc_end3-test_chained_indirect
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_fp_from_array              // -- Begin function test_fp_from_array
; CHECK: 	.type	test_fp_from_array,@function
; CHECK: test_fp_from_array:                     // @test_fp_from_array
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ slli32	r3, r1, 2; nop; lui	r2, funcs }
; CHECK: 	{ nop; nop; addi32_w	r2, r2, funcs }
; CHECK: 	{ s_lw_pre_reg	r3, r2, r3; nop; nop }
; CHECK: 	{ nop; nop; jalr	lr, r3, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end4:
; CHECK: 	.size	test_fp_from_array, .Lfunc_end4-test_fp_from_array
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_indirect_many_args         // -- Begin function test_indirect_many_args
; CHECK: 	.type	test_indirect_many_args,@function
; CHECK: test_indirect_many_args:                // @test_indirect_many_args
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 40; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	r3, sp, 24 }
; CHECK: 	{ nop; nop; st32	lr, r3, 0 }
; CHECK: 	{ nop; nop; st32	r10, r3, 4 }
; CHECK: 	{ nop; nop; st32	r9, r3, 8 }
; CHECK: 	{ nop; nop; st32	r8, r3, 12 }
; CHECK: 	.cfi_def_cfa_offset 40
; CHECK: 	.cfi_offset r8, 36
; CHECK: 	.cfi_offset r9, 32
; CHECK: 	.cfi_offset r10, 28
; CHECK: 	.cfi_offset lr, 24
; CHECK: 	{ subi32	sp, sp, 16; nop; addi32_w	r9, r0, 8 }
; CHECK: 	{ move32	r10, sp; nop; addi32_w	r8, r0, 1 }
; CHECK: 	{ nop; st32_post	r9, r10, 2; addi32_w	r12, r0, 2 }
; CHECK: 	{ move32	r9, r1; move32	r1, r8; st32	r2, r10, 0 }
; CHECK: 	{ nop; nop; addi32_w	r3, r0, 3 }
; CHECK: 	{ move32	r2, r12; nop; addi32_w	r4, r0, 4 }
; CHECK: 	{ nop; nop; addi32_w	r5, r0, 5 }
; CHECK: 	{ nop; nop; addi32_w	r6, r0, 6 }
; CHECK: 	{ nop; nop; addi32_w	r7, r0, 7 }
; CHECK: 	{ nop; nop; jalr	lr, r9, 0 }
; CHECK: 	{ xor32	r0, r0, r0; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 24; nop }
; CHECK: 	{ nop; ld32	r10, sp, 28; nop }
; CHECK: 	{ nop; ld32	r9, sp, 32; nop }
; CHECK: 	{ nop; ld32	r8, sp, 36; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 40 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end5:
; CHECK: 	.size	test_indirect_many_args, .Lfunc_end5-test_indirect_many_args
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_mixed_calls                // -- Begin function test_mixed_calls
; CHECK: 	.type	test_mixed_calls,@function
; CHECK: test_mixed_calls:                       // @test_mixed_calls
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 24; nop; nop }
; CHECK: 	{ nop; nop; addi32_w	r3, sp, 12 }
; CHECK: 	{ nop; nop; st32	lr, r3, 0 }
; CHECK: 	{ nop; nop; st32	r9, r3, 4 }
; CHECK: 	{ nop; nop; st32	r8, r3, 8 }
; CHECK: 	.cfi_def_cfa_offset 24
; CHECK: 	.cfi_offset r8, 20
; CHECK: 	.cfi_offset r9, 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ move32	r8, r1; move32	r1, r2; nop }
; CHECK: 	{ nop; nop; jal	lr, direct_callee }
; CHECK: 	{ xor32	r0, r0, r0; move32	r9, r1; nop }
; CHECK: 	{ nop; nop; jalr	lr, r8, 0 }
; CHECK: 	{ xor32	r0, r0, r0; add32	r1, r9, r1; nop }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; ld32	r9, sp, 16; nop }
; CHECK: 	{ nop; ld32	r8, sp, 20; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 24 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end6:
; CHECK: 	.size	test_mixed_calls, .Lfunc_end6-test_mixed_calls
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

@funcs = external global [4 x ptr]
declare i32 @direct_callee(i32)

define i32 @test_basic_indirect(ptr %fp, i32 %x) {

; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (S0-S1-S2 / setDesc members); .file skipped


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

define i32 @test_mixed_calls(ptr %fp, i32 %x) {
  %r1 = call i32 @direct_callee(i32 %x)
  %r2 = call i32 %fp(i32 %r1)
  %result = add i32 %r1, %r2
  ret i32 %result
}
