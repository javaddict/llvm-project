; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s
;
; Role: semantic — backend `'d'` is the DR64 file constraint (T-ABI12).
; Peer: RISCVISelLowering.cpp:24570 maps `'f'` to C_RegisterClass.
; Empty template pins IRTranslator + getConstraintType, not operand print.

define i64 @d_constraint_ok(i64 %x) nounwind {
; CHECK-LABEL: d_constraint_ok:
; CHECK: //APP
; CHECK: //NO_APP
  %r = call i64 asm sideeffect "", "=d,d"(i64 %x)
  ret i64 %r
}

define i64 @r_i64_binds_dr(i64 %x) nounwind {
; CHECK-LABEL: r_i64_binds_dr:
; CHECK: //APP
; CHECK: //NO_APP
  %r = call i64 asm sideeffect "", "=r,r"(i64 %x)
  ret i64 %r
}
