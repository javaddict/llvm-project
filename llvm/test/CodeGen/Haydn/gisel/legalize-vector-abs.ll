; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; G_ABS had no vector rule at all (only legalFor {s32, s64} + min/maxScalar), so
; an SLP-formed `<2 x s32> = G_ABS` aborted with "unable to legalize
; instruction". Finish the rule with .lower() like RISCV does: the generic ABS
; expansion re-enters the legalizer for the vector sub/max.
;
; X2ABS32 exists as a format but has no pattern for generic `abs`, so legalFor
; on V2I32 is not an option. Plain .scalarize(0) is not either — it reaches
; LegalizerHelper::fewerElementsVectorMerge, which asserts "Expected vector
; types" for a unary op.

declare <2 x i32> @llvm.abs.v2i32(<2 x i32>, i1)

; CHECK-LABEL: vabs_v2i32:
; CHECK: ld64
; CHECK: st64
define void @vabs_v2i32(ptr %p) nounwind {
  %v = load <2 x i32>, ptr %p, align 8
  %a = call <2 x i32> @llvm.abs.v2i32(<2 x i32> %v, i1 false)
  store <2 x i32> %a, ptr %p, align 8
  ret void
}

; Scalar forms keep the native ABS32 / ABS64 path.
declare i32 @llvm.abs.i32(i32, i1)
declare i64 @llvm.abs.i64(i64, i1)

; CHECK-LABEL: abs_i32:
; CHECK: abs32
define i32 @abs_i32(i32 %x) nounwind {
  %a = call i32 @llvm.abs.i32(i32 %x, i1 false)
  ret i32 %a
}

; CHECK-LABEL: abs_i64:
; CHECK: abs64
define i64 @abs_i64(i64 %x) nounwind {
  %a = call i64 @llvm.abs.i64(i64 %x, i1 false)
  ret i64 %a
}
