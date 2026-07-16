; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; ABS64S must emit (real FmtALU64Unary), not a dropped isPseudo stub.
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
