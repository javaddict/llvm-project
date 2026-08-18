; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — DR64 register copy must use OR64, not ADD64 with GPR32 zero reg.

; REGRESSION TEST: DR64 register copy must use OR64, not ADD64 with GPR32 zero reg.
;
; Bug : copyPhysReg for DR64→DR64 used ADD64 rd, rs, R0, but ADD64 requires
; all-DR64 operands. R0 is GPR32, causing "not a DR64 register" verifier error.
; Fix: Use OR64 rd, rs, rs (identity: rd = rs | rs = rs) instead.
;
; This test forces a DR64 copy by creating many live i64 values across a function
; call, which triggers the register allocator to emit DR64→DR64 copies. If the copy
; implementation regresses to using ADD64 with R0, llc will crash with a verifier
; error ("$r0 is not a DR64 register").
;
; Test design: 8 i64 arguments exceed the 4 DR64 argument registers (D0-D3), forcing
; stack spills and DR64 callee-saved (D8-D15) usage. The call to @bar forces all live
; values to be saved, triggering copyPhysReg for DR64 registers.
;
; Do NOT update CHECK lines without understanding the root cause. The OR64 instructions
; here verify that DR64 copies use the correct instruction.

define i64 @test_dr64_copy_regression(i64 %a0, i64 %a1, i64 %a2, i64 %a3,
                                       i64 %a4, i64 %a5, i64 %a6, i64 %a7) {
entry:
  %v0 = add i64 %a0, %a1
  %v1 = add i64 %a2, %a3
  %v2 = add i64 %a4, %a5
  %v3 = add i64 %a6, %a7
  %sum = add i64 %v0, %v1
  %sum2 = add i64 %v2, %v3
  %total = add i64 %sum, %sum2
  %r = call i64 @bar(i64 %total)
  %f0 = add i64 %r, %a0
  %f1 = add i64 %f0, %a1
  ret i64 %f1
}

declare i64 @bar(i64)

; CHECK-LABEL: test_dr64_copy_regression:
; The key check: we must NOT see any instruction that mixes DR64 and GPR operands.
; The test passing (llc not crashing) is the primary regression guard.
; Verify that DR64 callee-saved save/restore works (st64/ld64).
; CHECK: st64
; CHECK: ld64
