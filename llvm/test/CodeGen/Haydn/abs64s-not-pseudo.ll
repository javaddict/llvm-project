; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — ABS64S must emit via setDesc member materialize, not a
; dropped residual logical shell.

; ABS64S logical is non-encoding; private *_S1/*_S2 members must emit.
; Historical EMPTY_SEL: BundleSim intrin_sat64 failed at the first abs64s check.

declare i64 @llvm.haydn.abs64s(i64)

define i64 @abs64s_pos(i64 %a) {
; CHECK-LABEL: abs64s_pos:
; CHECK: // %bb.0:
; CHECK: abs64s
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.haydn.abs64s(i64 %a)
  ret i64 %r
}

define i64 @abs64s_imm() {
; CHECK-LABEL: abs64s_imm:
; CHECK: // %bb.0:
; CHECK: abs64s
; CHECK: jalr{{.*}}lr
entry:
  %r = call i64 @llvm.haydn.abs64s(i64 42)
  ret i64 %r
}
