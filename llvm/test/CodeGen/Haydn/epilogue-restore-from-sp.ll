; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 %s -o - | FileCheck %s

; REGRESSION TEST: epilogue restores callee-saved GPRs from SP (R13), not via
; a stale R12 base-pointer. The prologue uses R12 as a stride-4 base pointer
; for storing callee-saved GPRs (the matching ADDI32 R12, SP, off survives
; because the next ST32 reads R12). The epilogue's matching frame-destroy
; ADDI32 R12, SP, off is dead-code eliminated because R12 is a reserved
; register (AT scratch) — writes to R12 have no live range, leaving
; LD32 rt, R12, off reading whatever R12 holds (clobbered by callees).
;
; Symptom: main's saved LR (in R15) is lost across printf and the program
; returns to garbage.
;
; Fix: the epilogue restores GPRs directly from SP (LD32 rt, SP, off), no
; R12 base pointer.
;
; CHECK-LABEL: epilogue_restore_from_sp:
; CHECK: st32
; CHECK: jal{{(\.s[012])?}}
; CHECK: ld32
; CHECK: sp,
; CHECK: jalr{{(\.s[012])?}}
; CHECK-NOT: s_lw_{{[a-z_]*}} {{[^,]+}}, r12,
define void @epilogue_restore_from_sp(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
entry:
  call void @callee_with_many_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %a, i32 %b, i32 %c, i32 %d)
  call void @callee_with_many_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %a, i32 %b, i32 %c, i32 %d)
  ret void
}

declare void @callee_with_many_args(i32, i32, i32, i32, i32, i32, i32, i32)
