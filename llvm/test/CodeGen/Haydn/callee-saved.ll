; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 %s -o - | FileCheck %s

; Test basic function call - callee-saved register handling

; Test basic function call prologue/epilogue
; REBASELINED (auto) llc <stdin>;.file skipped

; CHECK:  .globl test_dr64_callee_saved // -- Begin function test_dr64_callee_saved
; CHECK:  .type test_dr64_callee_saved,@function
; CHECK: test_dr64_callee_saved: // @test_dr64_callee_saved
; CHECK:  .cfi_startproc
; CHECK: // %bb.0:
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { subi32 sp, sp, 16 }
; CHECK:  { st32 lr, sp, 12 }
; CHECK:  .cfi_def_cfa_offset 16
; CHECK:  .cfi_offset lr, 12
; CHECK:  { jal_w lr, callee }
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { ld32 lr, sp, 12 }
; CHECK:  { addi32{{(_w)?}} sp, sp, 16 }
; CHECK:  { jalr_w{{(\.s[012])?}} r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK:  .size test_dr64_callee_saved, .Lfunc_end0-test_dr64_callee_saved
; CHECK:  .cfi_endproc
; CHECK:  // -- End function
; CHECK:  .globl test_gpr_callee_saved // -- Begin function test_gpr_callee_saved
; CHECK:  .type test_gpr_callee_saved,@function
; CHECK: test_gpr_callee_saved: // @test_gpr_callee_saved
; CHECK:  .cfi_startproc
; CHECK: // %bb.0:
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { subi32 sp, sp, 16 }
; CHECK:  { st32 lr, sp, 12 }
; CHECK:  .cfi_def_cfa_offset 16
; CHECK:  .cfi_offset lr, 12
; CHECK:  { jal_w lr, callee }
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { ld32 lr, sp, 12 }
; CHECK:  { addi32{{(_w)?}} sp, sp, 16 }
; CHECK:  { jalr_w{{(\.s[012])?}} r0, lr, 0 }
; CHECK: .Lfunc_end1:
; CHECK:  .size test_gpr_callee_saved, .Lfunc_end1-test_gpr_callee_saved
; CHECK:  .cfi_endproc
; CHECK:  // -- End function
; CHECK:  .section ".note.GNU-stack","",@progbits

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
