; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s 2>&1 | FileCheck %s
;
; residual :
; PreLegalizer matchRedundantExt used to collapse G_SEXT(G_SEXT x)
; G_ZEXT(G_ZEXT x) into COPY of the *innermost* source, producing type
; mismatched COPY such as `s64 = COPY s1`. Upstream
; getIConstantVRegValWithLookThrough follows COPY without resizing the
; constant APInt; a later G_TRUNC then asserts
; APInt::trunc "Invalid APInt Truncate request"
; from isOperandImmEqual in HaydnPostLegalizerCombiner (full yarpgen seed
; at -O0).
;
; Fixes:
; 1) PreLegalizer: collapse double-ext to a single G_SEXT/G_ZEXT (or COPY
; only when types already match).
; 2) PostLegalizer sanitizeCastCopies: rewrite any remaining cross-type
; COPY / invalid G_TRUNC before match-table look-through.
;
; If either regresses, llc exits 134 before CHECK-LABEL on the seed shapes.

; CHECK-LABEL: double_zext_sext_bool:
define i64 @double_zext_sext_bool(i1 zeroext %b) nounwind {
entry:
  %z = zext i1 %b to i32
  %e = sext i32 %z to i64
  ret i64 %e
}

; CHECK-LABEL: nested_bool_int_long_and:
; Nested casts feeding an AND/-1 identity that exercises isOperandImmEqual
; look-through over the cast chain.
define i32 @nested_bool_int_long_and(i32 %x, i1 zeroext %b) nounwind {
entry:
  %z = zext i1 %b to i32
  %e = sext i32 %z to i64
  %t = trunc i64 %e to i32
  %a = and i32 %x, -1
  %o = or i32 %a, 0
  %r = add i32 %o, %t
  ret i32 %r
}

; CHECK-LABEL: triple_zext_chain:
define i64 @triple_zext_chain(i8 zeroext %v) nounwind {
entry:
  %a = zext i8 %v to i16
  %b = zext i16 %a to i32
  %c = zext i32 %b to i64
  ret i64 %c
}
