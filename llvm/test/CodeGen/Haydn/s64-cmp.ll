; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Test 64-bit equality comparison (low-half compare for MVB)
define i32 @cmp_eq_i64(i64 %a, i64 %b) {
; CHECK-LABEL: cmp_eq_i64:
; CHECK: seq32
  %cmp = icmp eq i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Test 64-bit inequality comparison
define i32 @cmp_ne_i64(i64 %a, i64 %b) {
; CHECK-LABEL: cmp_ne_i64:
; CHECK: seq32
  %cmp = icmp ne i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Test 64-bit unsigned less-than
define i32 @cmp_ult_i64(i64 %a, i64 %b) {
; CHECK-LABEL: cmp_ult_i64:
; CHECK: sltu32
  %cmp = icmp ult i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Test 64-bit signed less-than
; High half is SIGNED (slt32); low half is UNSIGNED (sltu32) — the low 32 bits
; of a 64-bit value carry no sign information, so the tie-break compare must be
; unsigned. (see signed-i64-low-half-sltu.ll for the low-half-unsigned regression).
define i32 @cmp_slt_i64(i64 %a, i64 %b) {
; CHECK-LABEL: cmp_slt_i64:
; CHECK-DAG: slt32
; CHECK-DAG: sltu32
  %cmp = icmp slt i64 %a, %b
  %r = zext i1 %cmp to i32
  ret i32 %r
}

; Test 64-bit comparison in branch
define void @cmp_branch_i64(i64 %a, i64 %b) {
; CHECK-LABEL: cmp_branch_i64:
; CHECK: seq32
entry:
  %cond = icmp eq i64 %a, %b
  br i1 %cond, label %then, label %else
then:
  ret void
else:
  ret void
}
