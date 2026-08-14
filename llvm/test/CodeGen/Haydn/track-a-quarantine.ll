; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Track A: inverse SLT stays two comparisons (not xor-not); basic smoke after formMACs/foldCmpBranch/LoadStoreOpt quarantine.

; Track A: inverse SLT stays two comparisons (not xor-not); basic smoke after
; formMACs/foldCmpBranch/LoadStoreOpt quarantine.

define i32 @inverse_slt_sum(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: inverse_slt_sum:
; CHECK-DAG: slt32
; CHECK-DAG: slt32
; CHECK: jalr
  %c1 = icmp slt i32 %a, %b
  %c2 = icmp slt i32 %b, %a
  %v1 = zext i1 %c1 to i32
  %v2 = zext i1 %c2 to i32
  %r = add i32 %v1, %v2
  ret i32 %r
}

define i32 @add_only(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: add_only:
; CHECK: add32
; CHECK: jalr
  %r = add i32 %a, %b
  ret i32 %r
}
