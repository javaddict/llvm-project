; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 %s -o - | FileCheck %s

; Test basic function call - callee-saved register handling

; Test basic function call prologue/epilogue


; REBASELINED (auto) B3.exit.4 Desc-only Bundle128 print (setDesc members; AIEBaseAsmPrinter field order); .file skipped

; CHECK: 	.text
; CHECK: 	.globl	test_dr64_callee_saved          // -- Begin function test_dr64_callee_saved
; CHECK: 	.type	test_dr64_callee_saved,@function
; CHECK: test_dr64_callee_saved:                 // @test_dr64_callee_saved
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal	lr, callee }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_dr64_callee_saved, .Lfunc_end0-test_dr64_callee_saved
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.globl	test_gpr_callee_saved           // -- Begin function test_gpr_callee_saved
; CHECK: 	.type	test_gpr_callee_saved,@function
; CHECK: test_gpr_callee_saved:                  // @test_gpr_callee_saved
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ subi32	sp, sp, 16; nop; nop }
; CHECK: 	{ nop; nop; st32	lr, sp, 12 }
; CHECK: 	.cfi_def_cfa_offset 16
; CHECK: 	.cfi_offset lr, 12
; CHECK: 	{ nop; nop; jal	lr, callee }
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ nop; ld32	lr, sp, 12; nop }
; CHECK: 	{ nop; nop; addi32_w	sp, sp, 16 }
; CHECK: 	{ nop; nop; jalr	r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK: 	.size	test_gpr_callee_saved, .Lfunc_end1-test_gpr_callee_saved
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function
; CHECK: 	.section	".note.GNU-stack","",@progbits

define void @test_dr64_callee_saved() {
  call void @callee()
  ret void
}

; Test basic function call prologue/epilogue
define void @test_gpr_callee_saved() {
  call void @callee()
  ret void
}

declare void @callee()
