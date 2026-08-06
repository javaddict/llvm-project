; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — residual: G_TRUNC to non-pow2 result widths must legalize.

; residual: G_TRUNC to non-pow2 result widths must legalize.
; yarpgen seed 2896 aborted on `%_(s24) = G_TRUNC %_(s32)` (23-bit signed
; bitfields lower as i24). Closed rule:
; legalizer: remaining scalar G_TRUNC is legal (bit-subset on GPR/DR)
; ZEXT/SEXT from any sub-32 scalar (incl. s24) legal; selector masks
; shift-sign-extends by SrcBits (not a hardcoded {1,8,16} set).
;
; Companion of cb114-trunc-to-s1.ll (s1-result lattice).

; s32 -> s24 trunc used via zext (forces mask / shift clean)

define i32 @zext_trunc_s24(i32 %x) nounwind {
  %t = trunc i32 %x to i24
  %z = zext i24 %t to i32
  ret i32 %z
}

; s32 -> s24 trunc used via sext
; CHECK-LABEL: sext_trunc_s24:
; CHECK-DAG: sll32
; CHECK-DAG: sra32
define i32 @sext_trunc_s24(i32 %x) nounwind {
  %t = trunc i32 %x to i24
  %s = sext i24 %t to i32
  ret i32 %s
}

; arithmetic on s24 must not abort legalization
; CHECK-LABEL: add_trunc_s24:
define i32 @add_trunc_s24(i32 %x, i32 %y) nounwind {
  %a = trunc i32 %x to i24
  %b = trunc i32 %y to i24
  %s = add i24 %a, %b
  %z = zext i24 %s to i32
  ret i32 %z
}

; s64 -> s24
; CHECK-LABEL: trunc_s64_to_s24:
; CHECK: move32_dr_l
define i32 @trunc_s64_to_s24(i64 %x) nounwind {
  %t = trunc i64 %x to i24
  %z = zext i24 %t to i32
  ret i32 %z
}

; other non-pow2 widths (s12, s31)
; CHECK-LABEL: zext_trunc_s12:
define i32 @zext_trunc_s12(i32 %x) nounwind {
  %t = trunc i32 %x to i12
  %z = zext i12 %t to i32
  ret i32 %z
}

; CHECK-LABEL: zext_trunc_s31:
define i32 @zext_trunc_s31(i32 %x) nounwind {
  %t = trunc i32 %x to i31
  %z = zext i31 %t to i32
  ret i32 %z
}

; Smoke: bare trunc-to-s24 must not abort (-global-isel-abort=1 is the guard).
define i24 @smoke_trunc_s24(i32 %x) nounwind {
  %t = trunc i32 %x to i24
  ret i24 %t
}
