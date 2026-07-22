; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; G_MUL s32 → MULL (golden MAC GRR). CHECK-NOT phantom mul32 / libcall.

define i32 @scalar_mul(i32 %a, i32 %b) {
; CHECK-LABEL: scalar_mul:
; CHECK: mull
; CHECK-NOT: mul32
; CHECK-NOT: mul64
; CHECK-NOT: __mul
entry:
  %m = mul i32 %a, %b
  ret i32 %m
}

; Conditional multiply (shape: `if (n & 1) h = h * x;`).
define i32 @cond_mul(i32 %n, i32 %h, i32 %x) {
; CHECK-LABEL: cond_mul:
; CHECK: mull
; CHECK-NOT: mul32
; CHECK-NOT: __mul
entry:
  %bit = and i32 %n, 1
  %cmp = icmp eq i32 %bit, 0
  %mh = mul i32 %h, %x
  %sel = select i1 %cmp, i32 %h, i32 %mh
  ret i32 %sel
}
