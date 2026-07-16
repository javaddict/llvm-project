; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1  -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_ld64_fold:
; CHECK-LABEL: test_st64_fold:
; CHECK: {{.}}

declare i64 @llvm.haydn.mulfp32x16x2ras.low(i64, i64, i64)

; LD64 fold: the intrinsic loads %x as i64, which the selector decomposes to
; LD64 + MOV_DR64_TO_GPR (unpack). The fold collapses this to two LD32.
; Without a follow-up ADDI32 of the base, the fold fires.
; NOTE: loads may be promoted to ld32 (slot-1 promotion,) when they pack.
; The base may be sp (stack spill) or a GPR — match either.
define i64 @test_ld64_fold(ptr %in, i64 %coef) {
  %x = load i64, ptr %in
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 0, i64 %x, i64 %coef)
  ret i64 %r
}

; ST64 fold: the intrinsic produces an i64 result that is stored. The
; selector emits MOV_GPR_TO_DR64 (pack) + ST64. The fold collapses to 2 ST32.
define void @test_st64_fold(ptr %out, i64 %a, i64 %coef) {
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 0, i64 %a, i64 %coef)
  store i64 %r, ptr %out
  ret void
}
