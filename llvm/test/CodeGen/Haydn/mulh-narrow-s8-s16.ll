; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Narrow high-half multiply patterns (yarpgen seeds 1, 6, 10).
; s8/s16 high product via extend + MULL + shift.

define i8 @smulh_s8(i8 %a, i8 %b) nounwind {
; CHECK-LABEL: smulh_s8:
; CHECK-DAG: {{sll32|sra32}}
; CHECK-DAG: mull
; CHECK-DAG: {{srl32|sra32}}
  %aa = sext i8 %a to i16
  %bb = sext i8 %b to i16
  %m = mul i16 %aa, %bb
  %trunc = lshr i16 %m, 8
  %r = trunc i16 %trunc to i8
  ret i8 %r
}

define i8 @umulh_s8(i8 %a, i8 %b) nounwind {
; CHECK-LABEL: umulh_s8:
; CHECK-DAG: mull
; CHECK-DAG: srl32
  %aa = zext i8 %a to i16
  %bb = zext i8 %b to i16
  %m = mul i16 %aa, %bb
  %trunc = lshr i16 %m, 8
  %r = trunc i16 %trunc to i8
  ret i8 %r
}

define i16 @smulh_s16(i16 %a, i16 %b) nounwind {
; CHECK-LABEL: smulh_s16:
; CHECK-DAG: mull
; CHECK-DAG: srl32
  %aa = sext i16 %a to i32
  %bb = sext i16 %b to i32
  %m = mul i32 %aa, %bb
  %r = lshr i32 %m, 16
  %tr = trunc i32 %r to i16
  ret i16 %tr
}

define i16 @umulh_s16(i16 %a, i16 %b) nounwind {
; CHECK-LABEL: umulh_s16:
; CHECK-DAG: mull
; CHECK-DAG: srl32
  %aa = zext i16 %a to i32
  %bb = zext i16 %b to i32
  %m = mul i32 %aa, %bb
  %r = lshr i32 %m, 16
  %tr = trunc i32 %r to i16
  ret i16 %tr
}

define i8 @smulh_s8_neg(i8 %a, i8 %b) nounwind {
; CHECK-LABEL: smulh_s8_neg:
; CHECK-DAG: mull
  %aa = sext i8 %a to i16
  %bb = sext i8 %b to i16
  %m = mul i16 %aa, %bb
  %shifted = lshr i16 %m, 8
  %r = trunc i16 %shifted to i8
  ret i8 %r
}
