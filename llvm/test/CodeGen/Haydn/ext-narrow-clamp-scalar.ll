; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — G_SEXT / G_ZEXT / G_ANYEXT for narrow destinations (s1->s8 s1->s16, s8->s16) must lower via clampScalar(0, S32, S64): the destination.

; REGRESSION TEST: G_SEXT / G_ZEXT / G_ANYEXT for narrow destinations (s1->s8
; s1->s16, s8->s16) must lower via clampScalar(0, S32, S64): the destination
; widens to s32 first (which the selector handles natively), then truncates.
; Previously, the legalizer declared s8->s1 legal but missed s1->s8/s16 and
; s8->s16, causing "unable to legalize G_ZEXT s1->s16" (LC3 attack_detector_fx)
; and "cannot select G_ZEXT s8->s16" at InstructionSelect (LC3 al_fec).


define i8 @zext_i1_to_i8(i1 %x) nounwind {
  %r = zext i1 %x to i8
  ret i8 %r
}

; CHECK-LABEL: zext_i1_to_i16:
; CHECK: and32
define i16 @zext_i1_to_i16(i1 %x) nounwind {
  %r = zext i1 %x to i16
  ret i16 %r
}

; CHECK-LABEL: sext_i1_to_i8:
; CHECK: sll32
; CHECK: sra32
define i8 @sext_i1_to_i8(i1 %x) nounwind {
  %r = sext i1 %x to i8
  ret i8 %r
}

; CHECK-LABEL: zext_i8_to_i16:
; CHECK: and32
define i16 @zext_i8_to_i16(i8 %x) nounwind {
  %r = zext i8 %x to i16
  ret i16 %r
}

; CHECK-LABEL: sext_i8_to_i16:
; CHECK: sll32
; CHECK: sra32
define i16 @sext_i8_to_i16(i8 %x) nounwind {
  %r = sext i8 %x to i16
  ret i16 %r
}
