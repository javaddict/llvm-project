; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s 2>&1 | FileCheck %s
;
; wave-1 (Legalizer): nested trunc/ext cast chains at -O0 must not
; crash in LegalizationArtifactCombiner::tryCombineTrunc via
; MachineIRBuilder::validateTruncExt ("invalid widening trunc").
;
; Root: unconditional trunc(trunc)->trunc fold when an earlier artifact
; combine narrowed the inner source so TruncSrc is not strictly wider than
; DstTy. Guard (LegalizationArtifactCombiner.h):
; if (MRI.getType(TruncSrc).getSizeInBits <= DstTy.getSizeInBits)
; return false;
;
; Scoped exception for that one guard — see OPEN-COMPILER-BUGS.md.
; Companion: cb61-o0-postlegalizer-apint-width.ll (PostLegalizer APInt widths).
;
; If this regresses, llc exits 134 before CHECK-LABEL.

; CHECK-LABEL: yarpgen_style_nested_casts:
define i64 @yarpgen_style_nested_casts(i64 %x, i1 zeroext %y) nounwind {
entry:
  ; (long long)X >> (long long)(int)(bool)Y
  %yi = zext i1 %y to i32
  %ye = sext i32 %yi to i64
  %yt = trunc i64 %ye to i32
  %ya = zext i32 %yt to i64
  %r = ashr i64 %x, %ya
  ret i64 %r
}

; CHECK-LABEL: trunc_trunc_through_zext:
define i32 @trunc_trunc_through_zext(i64 %x) nounwind {
entry:
  %t1 = trunc i64 %x to i32
  %z = zext i32 %t1 to i64
  %t2 = trunc i64 %z to i16
  %e = sext i16 %t2 to i32
  ret i32 %e
}

; CHECK-LABEL: bool_int_long_roundtrip:
define i32 @bool_int_long_roundtrip(i1 zeroext %b) nounwind {
entry:
  %z1 = zext i1 %b to i32
  %e1 = sext i32 %z1 to i64
  %t1 = trunc i64 %e1 to i32
  %e2 = zext i32 %t1 to i64
  %t2 = trunc i64 %e2 to i32
  ret i32 %t2
}
