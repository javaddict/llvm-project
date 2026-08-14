; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — G_ICMP on non-power-of-2 scalar types (s61/s63 from bitfields yarpgen) must widen to the next power-of-2 before legalization, not fail.

; G_ICMP on non-power-of-2 scalar types (s61/s63 from bitfields
; yarpgen) must widen to the next power-of-2 before legalization, not fail
; with "unable to legalize instruction: G_ICMP... s63".
;
; clampScalar(1, S32, S64) alone leaves sizes in (32, 64) untouched; the
; legalizer needs widenScalarToNextPow2(1) first (AArch64/RISC-V pattern).
; LegalizerHelper chooses SEXT vs ZEXT from the predicate.

; Unsigned ugt on i63 → widen both sides to i64 via ZEXT, then 64-bit compare.

define i1 @icmp_ugt_i63(i63 %a, i63 %b) nounwind {
  %c = icmp ugt i63 %a, %b
  ret i1 %c
}

; Signed slt on i61 → widen both sides to i64 via SEXT, then 64-bit compare.
; CHECK-LABEL: icmp_slt_i61:
; CHECK-DAG: slt32
; CHECK: jalr{{(\.s[012])?}}
define i1 @icmp_slt_i61(i61 %a, i61 %b) nounwind {
  %c = icmp slt i61 %a, %b
  ret i1 %c
}

; Unsigned ult on i61 (yarpgen seed 2168 shape).
; CHECK-LABEL: icmp_ult_i61:
; CHECK-DAG: sltu32
; CHECK: jalr{{(\.s[012])?}}
define i1 @icmp_ult_i61(i61 %a, i61 %b) nounwind {
  %c = icmp ult i61 %a, %b
  ret i1 %c
}

; Equality on a mid-range non-pow2 (s48) — also hits the same rule.
; CHECK-LABEL: icmp_eq_i48:
; CHECK-DAG: seq32
; CHECK: jalr{{(\.s[012])?}}
define i1 @icmp_eq_i48(i48 %a, i48 %b) nounwind {
  %c = icmp eq i48 %a, %b
  ret i1 %c
}
