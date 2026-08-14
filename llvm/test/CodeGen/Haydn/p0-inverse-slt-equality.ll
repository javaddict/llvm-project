; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — counterexample: when a == b, (a < b) = (b < a) = 0, so (b < a) != !(a < b).

; counterexample: when a == b, (a < b) = (b < a) = 0, so
; (b < a) != !(a < b). Inverse-reuse must not fire; both SLT remain.
; Value check: sum of the two bools at a==b must be 0 (not 1).

define i32 @both_slt_at_eq(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: both_slt_at_eq:
; CHECK-DAG: slt32
; CHECK-DAG: slt32
; CHECK: jalr
  %cmp1 = icmp slt i32 %a, %b
  %cmp2 = icmp slt i32 %b, %a
  %v1 = zext i1 %cmp1 to i32
  %v2 = zext i1 %cmp2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}
