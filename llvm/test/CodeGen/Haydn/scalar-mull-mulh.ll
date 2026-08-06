; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Golden scalar GPR32 multiply family (MAC GRR): G_MUL s32 → MULL (low 32 of product, wrap).

; Golden scalar GPR32 multiply family (MAC GRR):
;   G_MUL s32 → MULL (low 32 of product, wrap)

define i32 @g_mul_s32(i32 %a, i32 %b) {
; CHECK-LABEL: g_mul_s32:
; CHECK: mull
; CHECK-NOT: mul64
; CHECK-NOT: mulssh
  %r = mul i32 %a, %b
  ret i32 %r
}

; Widening high-half stays on s64 MUL64 path (not MULL low-half).
define i32 @smulh_via_widen(i32 %a, i32 %b) {
; CHECK-LABEL: smulh_via_widen:
; CHECK: mul64
; CHECK-NOT: mull
  %ea = sext i32 %a to i64
  %eb = sext i32 %b to i64
  %p = mul i64 %ea, %eb
  %h = lshr i64 %p, 32
  %r = trunc i64 %h to i32
  ret i32 %r
}

define i32 @umulh_via_widen(i32 %a, i32 %b) {
; CHECK-LABEL: umulh_via_widen:
; CHECK: mul64
  %ea = zext i32 %a to i64
  %eb = zext i32 %b to i64
  %p = mul i64 %ea, %eb
  %h = lshr i64 %p, 32
  %r = trunc i64 %h to i32
  ret i32 %r
}
