; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; T1.2: G_ABS maps to native ABS32 / ABS64 (not generic lower expansion).
; Non-saturating ISA ops match llvm.abs (INT_MIN stays INT_MIN).

declare i32 @llvm.abs.i32(i32, i1 immarg)
declare i64 @llvm.abs.i64(i64, i1 immarg)

define i32 @abs32(i32 %a) {
; CHECK-LABEL: abs32:
; CHECK: // %bb.0:
; CHECK: abs32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.abs.i32(i32 %a, i1 false)
  ret i32 %r
}

define i32 @abs32_intmin_poison(i32 %a) {
; CHECK-LABEL: abs32_intmin_poison:
; CHECK: // %bb.0:
; CHECK: abs32
; CHECK: jalr{{.*}}lr
entry:
  %r = call i32 @llvm.abs.i32(i32 %a, i1 true)
  ret i32 %r
}

define i64 @abs64(i64 %a) {
; CHECK-LABEL: abs64:
; CHECK: // %bb.0:
; CHECK: abs64
; CHECK-NOT: abs64s
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.abs.i64(i64 %a, i1 false)
  ret i64 %r
}

define i64 @abs64_intmin_poison(i64 %a) {
; CHECK-LABEL: abs64_intmin_poison:
; CHECK: // %bb.0:
; CHECK: abs64
; CHECK-NOT: abs64s
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.abs.i64(i64 %a, i1 true)
  ret i64 %r
}
