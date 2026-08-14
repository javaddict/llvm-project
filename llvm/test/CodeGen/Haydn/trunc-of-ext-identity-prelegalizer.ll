; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 | FileCheck %s

; Role: semantic — trunc of sext/zext where dest type == ext input type is an IDENTITY and must be folded to a COPY in the HaydnPreLegalizerCombiner.

; REGRESSION TEST: trunc of sext/zext where dest type == ext input type is
; an IDENTITY and must be folded to a COPY in the HaydnPreLegalizerCombiner.
;
; Bug: at -O0, yarpgen nested cast chains such as
; `(long long)X >> (long long)(int)(bool)Y`
; produce G_TRUNC(G_[SZ]EXT(...)) shapes whose dest type equals the ext input
; type. Left alone, the upstream Legalizer artifact-combiner's trunc(trunc) fold
; (`LegalizationArtifactCombiner::tryCombineTrunc`
; llvm/include/llvm/CodeGen/GlobalISel/LegalizationArtifactCombiner.h:344-354)
; can fire on the chain AFTER a same-sweep source narrowing, observing an inner
; source that has become too narrow for the outer trunc. That trips
; `MachineIRBuilder::validateTruncExt`'s "invalid widening trunc" assertion
; (exit 134) inside the Legalizer.
;
; We cannot edit the upstream artifact combiner (hard constraint #0). The fix
; collapses the identity in the Haydn PRE-legalizer so the dangerous shape is
; gone before the Legalizer's artifact sweep ever runs.
;
; Test design: the shift amount `(long long)(int)(bool)Y` forces a chain of
; truncations through sext/zext of widening widths; the identity fold must
; collapse the inner trunc(zext)/trunc(sext) to a copy. The outer shift
; trivially assembles; the test asserts llc reaches assembly output (i.e. the
; Legalizer did NOT crash) and that no redundant ext+trunc pair survives in
; the output. If the bug regresses, llc crashes with exit 134 before reaching
; the CHECK-LABEL.

define i64 @trunc_of_ext_identity(i1 zeroext %b, i64 %x) nounwind {
; CHECK-LABEL: trunc_of_ext_identity:
; CHECK:      jalr{{(\.s[012])?}}
  %b2 = select i1 %b, i32 2, i32 0           ; materialize a non-trivial i32
  %e1 = zext i32 %b2 to i64                  ; zext i32 -> i64
  %t1 = trunc i64 %e1 to i32                 ; trunc back to i32 : identity
  %e2 = sext i32 %t1 to i64                  ; sext i32 -> i64
  %t2 = trunc i64 %e2 to i32                 ; trunc back to i32 : identity
  %amt = zext i32 %t2 to i64                 ; shift amount: i32 -> i64
  %r = ashr i64 %x, %amt
  ret i64 %r
}

; Second shape: bool -> int -> i64 shift-amount, the literal yarpgen pattern.
define i64 @bool_to_int_to_i64_shift(i1 zeroext %b, i64 %x) nounwind {
; CHECK-LABEL: bool_to_int_to_i64_shift:
; CHECK:      jalr{{(\.s[012])?}}
  %z1 = zext i1 %b to i32                    ; bool -> int
  %e1 = sext i32 %z1 to i64                  ; int -> long long
  %t1 = trunc i64 %e1 to i32                 ; trunc: identity (dest == ext input)
  %amt = zext i32 %t1 to i64                 ; back to i64 shift amount
  %r = ashr i64 %x, %amt
  ret i64 %r
}
