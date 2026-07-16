; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s 2>&1 | FileCheck %s
;
; (PostLegalizer residual,):
; After the LegalizationArtifactCombiner trunc(trunc) guard landed, the yarpgen
; seed still crashed in HaydnPostLegalizerCombiner with
; APInt::operator== / APInt::trunc
; "Comparison requires equal bit widths" / "Invalid APInt Truncate request"
; because getIConstantVRegValWithLookThrough can surface constants whose
; APInt bit widths differ from the consuming instruction's LLT (trunc/zext
; look-through at -O0). All constant folds must normalize widths before
; APInt arithmetic / comparison.
;
; These functions are the reduced shapes that exercise the fixed matchers
; (icmp const-fold, and/or -1, mul by 1/-1, add-const chain, xor-xor fold
; and/or disjoint, shift-mask). If the width guards regress, llc aborts
; before emitting a function label.

; CHECK-LABEL: const_icmp_and_or_mul:
define i32 @const_icmp_and_or_mul(i32 %x) nounwind {
entry:
  %c = icmp eq i32 0, 0
  %z = zext i1 %c to i32
  %a = and i32 %x, -1
  %o = or i32 %a, 0
  %m = mul i32 %o, 1
  %n = mul i32 %m, -1
  %s = add i32 %n, 5
  %s2 = add i32 %s, 3
  %x1 = xor i32 %s2, -1
  %x2 = xor i32 %x1, -1
  %r = add i32 %x2, %z
  ret i32 %r
}

; CHECK-LABEL: trunc_ext_identity_shift:
; Nested trunc/sext/zext that historically tripped Legalizer validateTruncExt
; (LegalizationArtifactCombiner trunc(trunc) without width guard) and then
; PostLegalizer APInt width asserts once that was fixed.
define i64 @trunc_ext_identity_shift(i1 zeroext %b, i64 %x) nounwind {
entry:
  %b2 = select i1 %b, i32 2, i32 0
  %e1 = zext i32 %b2 to i64
  %t1 = trunc i64 %e1 to i32
  %e2 = sext i32 %t1 to i64
  %t2 = trunc i64 %e2 to i32
  %amt = zext i32 %t2 to i64
  %r = ashr i64 %x, %amt
  ret i64 %r
}

; CHECK-LABEL: and_or_disjoint_bits:
; (A & 0x0F) | 0xFFFFFFF0 with disjoint mask/set — MatchInfo carries two
; APInts that must share the result bit width.
define i32 @and_or_disjoint_bits(i32 %a) nounwind {
entry:
  %m = and i32 %a, 15
  %o = or i32 %m, -16
  ret i32 %o
}

; CHECK-LABEL: xor_allones_value_use:
; Clean zext-of-icmp boolean: XOR with -1 must not APInt-compare mismatched
; widths in matchXorAllOnesBoolean / matchAndAllOnes.
define i32 @xor_allones_value_use(i32 %a, i32 %b) nounwind {
entry:
  %c = icmp sgt i32 %a, %b
  %z = zext i1 %c to i32
  %r = xor i32 %z, -1
  ret i32 %r
}

; CHECK-LABEL: lshr_and_low_mask:
; (x >> 8) & 0x00FFFFFF — shift-mask redundancy match compares two APInts.
define i32 @lshr_and_low_mask(i32 %x) nounwind {
entry:
  %s = lshr i32 %x, 8
  %a = and i32 %s, 16777215
  ret i32 %a
}
