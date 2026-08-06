; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s

; Role: semantic — G_TRUNC to s1 must legalize from every scalar width we otherwise allow as a source (s8/s16/s32/s64).

; G_TRUNC to s1 must legalize from every scalar width we otherwise
; allow as a source (s8/s16/s32/s64). Legalizer was missing {S1, S16} →
; "unable to legalize G_TRUNC s16→s1" (yarpgen seed 2542).
;
; Closed rule:
; legalFor: {S1, S8/S16/S32/S64} (s1-result column complete)
; selector: any trunc-to-s1 → AND with 1 (s64 also MOVE32_DR_L first)
;
; Bare replaceRegWith is wrong for G_BRCOND (nonzero) / MOVT32 (bit0) on
; e.g. trunc i16 2 to i1. CHECKs force a *use* of the s1 so the AND is
; not DCE'd (pure `ret i1` currently drops the return value — separate ABI).

; s16 → s1 (the gap) used as select predicate

define i32 @select_trunc_s16(i16 %x, i32 %a, i32 %b) nounwind {
  %c = trunc i16 %x to i1
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; s8 → s1
; CHECK-LABEL: select_trunc_s8:
; CHECK: and32
; CHECK: movt32
define i32 @select_trunc_s8(i8 %x, i32 %a, i32 %b) nounwind {
  %c = trunc i8 %x to i1
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; s32 → s1
; CHECK-LABEL: select_trunc_s32:
; CHECK: and32
; CHECK: movt32
define i32 @select_trunc_s32(i32 %x, i32 %a, i32 %b) nounwind {
  %c = trunc i32 %x to i1
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; s64 → s1
; CHECK-LABEL: select_trunc_s64:
; CHECK-DAG: move32_dr_l
; CHECK-DAG: and32
; CHECK: movt32
define i32 @select_trunc_s64(i64 %x, i32 %a, i32 %b) nounwind {
  %c = trunc i64 %x to i1
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Smoke: legalize must not abort on a bare trunc-to-s1 (any width).
; No CHECK body — the RUN line's -global-isel-abort=1 is the guard.
define i1 @smoke_trunc_s16_to_s1(i16 %x) nounwind {
  %t = trunc i16 %x to i1
  ret i1 %t
}
