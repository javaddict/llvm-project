; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Overflow intrinsics return { result, i1 flag }. After RetCC multi-field
; fix, both fields are live: result in R1, flag in R2.
; B3.exit.4: Desc-only glued mnemonics (add32r1 / sltu32r2).

; NOTE: G_SMULO / G_UMULO still unsupported (default legalizer needs division).

define { i32, i1 } @sadd_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: sadd_overflow:
; CHECK-DAG:       add32
; CHECK-DAG:       slt32
; CHECK-DAG:       xor32
  %result = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

define { i32, i1 } @uadd_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: uadd_overflow:
; CHECK-DAG:       add32
; CHECK:       sltu32
  %result = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

define { i32, i1 } @ssub_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: ssub_overflow:
; CHECK:       sub32
; CHECK-DAG:       slt32
  %result = call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

define { i32, i1 } @usub_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: usub_overflow:
; CHECK-DAG:       sub32
; CHECK-DAG:       sltu32
  %result = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

define i32 @checked_add(i32 %a, i32 %b) {
; CHECK-LABEL: checked_add:
; CHECK-DAG:       add32
; CHECK:       movt32
  %res = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %val = extractvalue { i32, i1 } %res, 0
  %ovf = extractvalue { i32, i1 } %res, 1
  %result = select i1 %ovf, i32 -1, i32 %val
  ret i32 %result
}

define i32 @chained_overflow(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: chained_overflow:
; CHECK-DAG:       add32
; CHECK:       or32
; CHECK:       movt32
  %r1 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %v1 = extractvalue { i32, i1 } %r1, 0
  %o1 = extractvalue { i32, i1 } %r1, 1
  %r2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %v1, i32 %c)
  %v2 = extractvalue { i32, i1 } %r2, 0
  %o2 = extractvalue { i32, i1 } %r2, 1
  %any_ovf = or i1 %o1, %o2
  %result = select i1 %any_ovf, i32 0, i32 %v2
  ret i32 %result
}

declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.uadd.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.ssub.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.usub.with.overflow.i32(i32, i32)
