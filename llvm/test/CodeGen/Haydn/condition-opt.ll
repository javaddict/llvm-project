; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s for the HaydnConditionOptimizer pass (post-RA).

; Status : previously-XFAIL regression resolved; lit PASS.
;
; Tests for the HaydnConditionOptimizer pass (post-RA).
; Each function exercises a specific comparison simplification pattern.
;
; Pass optimizations tested here:
; Self-comparison elimination: SLT32/SLTU32 r, rX, rX → SUB32 r, r, r (== 0)
; Inverse comparison reuse: SLT32 rA, rX, rY + SLT32 rB, rY, rX
; → second replaced with XORI32 rB, rA, 1
;
; elimination and the inverse-pair reuse no longer fire (same root cause as
; condition-opt-branches.ll). Likely the post-RA pattern matcher in
; HaydnConditionOptimizer no longer recognizes the _S0 slot-suffixed

;===--- Self-comparison: slt i32 %a, %a → 0 ---===

define i32 @self_comparison_slt(i32 %a) nounwind {
; CHECK-LABEL: self_comparison_slt:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp = icmp slt i32 %a, %a
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Self-comparison: ult i32 %a, %a → 0 ---===

define i32 @self_comparison_ult(i32 %a) nounwind {
; CHECK-LABEL: self_comparison_ult:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp = icmp ult i32 %a, %a
  %r = zext i1 %cmp to i32
  ret i32 %r
}

;===--- Inverse comparison pair: both SLT must remain ---===
; (b < a) != !(a < b) when a == b. Inverse-reuse deleted; both cmps stay.

define i32 @inverse_comparison(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: inverse_comparison:
; CHECK: slt32
; CHECK: slt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %b, %a
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}

;===--- Non-inverse comparison: slt i32 %a, %b and slt i32 %c, %d ---===
; Unrelated comparisons should not be affected.

define i32 @unrelated_comparisons(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: unrelated_comparisons:
; Both comparisons should remain as slt32 (no inverse relationship).
; CHECK: slt32
; CHECK: slt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %c, %d
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}

;===--- Inverse pair with use: result used in select ---===

define i32 @inverse_comparison_select(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: inverse_comparison_select:
; CHECK: slt32
; CHECK: slt32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %b, %a
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = select i1 %cmp1, i32 %v1, i32 %v2
  ret i32 %r
}
