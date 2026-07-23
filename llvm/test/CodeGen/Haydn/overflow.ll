; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Overflow intrinsics return { result, i1 flag }. After RetCC multi-field
; fix, both fields are live: result in R1, flag in R2.
; Previously only VRegs[0] was returned so the flag was dropped and the
; overflow-check instructions looked dead (old CHECKs expected a bare add).

; NOTE: G_SMULO / G_UMULO still unsupported (default legalizer needs division).

;sadd_with_overflow: result R1, signed-overflow flag R2
define { i32, i1 } @sadd_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: sadd_overflow:
; CHECK:       add32{{(\.s[012])?}}
; CHECK:       slt32{{(\.s[012])?}}
; CHECK:       xor32{{(\.s[012])?}}
  %result = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

;uadd_with_overflow: result R1, carry flag R2 (sltu)
define { i32, i1 } @uadd_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: uadd_overflow:
; CHECK:       add32{{(\.s[012])?}} r1, r1, r2
; CHECK:       sltu32{{(\.s[012])?}} r2, r1, r2
  %result = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

;ssub_with_overflow
define { i32, i1 } @ssub_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: ssub_overflow:
; CHECK:       sub32{{(\.s[012])?}}
; CHECK:       slt32{{(\.s[012])?}}
  %result = call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

;usub_with_overflow
define { i32, i1 } @usub_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: usub_overflow:
; CHECK:       sub32{{(\.s[012])?}}
; CHECK:       sltu32{{(\.s[012])?}}
  %result = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %result
}

;Use overflow result → select on flag
define i32 @checked_add(i32 %a, i32 %b) {
; CHECK-LABEL: checked_add:
; CHECK:       add32{{(\.s[012])?}}
; CHECK:       movt32{{(\.s[012])?}}
  %res = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %val = extractvalue { i32, i1 } %res, 0
  %ovf = extractvalue { i32, i1 } %res, 1
  %result = select i1 %ovf, i32 -1, i32 %val
  ret i32 %result
}

;Chained overflow checks
define i32 @chained_overflow(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: chained_overflow:
; CHECK:       add32{{(\.s[012])?}}
; CHECK:       or32{{(\.s[012])?}}
; CHECK:       movt32{{(\.s[012])?}}
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
