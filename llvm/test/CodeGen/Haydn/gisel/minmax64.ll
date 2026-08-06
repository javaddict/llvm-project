; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — T1.3: G_SMAX/G_SMIN s64 → native MAX64/MIN64 (not generic lower expansion).

; T1.3: G_SMAX/G_SMIN s64 → native MAX64/MIN64 (not generic lower expansion).
; G_UMAX/G_UMIN s64 stay lowered (no MAXU64/MINU64 in ISA) — smoke only that
; they still compile under -global-isel-abort=1 without selecting max64/min64.

declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.smin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)
declare i32 @llvm.umin.i32(i32, i32)
declare i64 @llvm.smax.i64(i64, i64)
declare i64 @llvm.smin.i64(i64, i64)
declare i64 @llvm.umax.i64(i64, i64)
declare i64 @llvm.umin.i64(i64, i64)

define i32 @smax32(i32 %a, i32 %b) {
; CHECK-LABEL: smax32:
; CHECK: // %bb.0:
; CHECK: max32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.smax.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @smin32(i32 %a, i32 %b) {
; CHECK-LABEL: smin32:
; CHECK: // %bb.0:
; CHECK: min32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.smin.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @umax32(i32 %a, i32 %b) {
; CHECK-LABEL: umax32:
; CHECK: // %bb.0:
; CHECK: maxu32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @umin32(i32 %a, i32 %b) {
; CHECK-LABEL: umin32:
; CHECK: // %bb.0:
; CHECK: minu32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i64 @smax64(i64 %a, i64 %b) {
; CHECK-LABEL: smax64:
; CHECK: // %bb.0:
; CHECK: max64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.smax.i64(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @smin64(i64 %a, i64 %b) {
; CHECK-LABEL: smin64:
; CHECK: // %bb.0:
; CHECK: min64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.smin.i64(i64 %a, i64 %b)
  ret i64 %r
}

; Unsigned s64: no native MAXU64/MINU64 — must not emit max64/min64.
define i64 @umax64(i64 %a, i64 %b) {
; CHECK-LABEL: umax64:
; CHECK: // %bb.0:
; CHECK-NOT: max64
; CHECK-NOT: min64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.umax.i64(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @umin64(i64 %a, i64 %b) {
; CHECK-LABEL: umin64:
; CHECK: // %bb.0:
; CHECK-NOT: max64
; CHECK-NOT: min64
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.umin.i64(i64 %a, i64 %b)
  ret i64 %r
}
