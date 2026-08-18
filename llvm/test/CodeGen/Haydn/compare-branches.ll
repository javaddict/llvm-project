; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — all comparison predicates with branches.

; Test all comparison predicates with branches

;ICMP_EQ (equal)

define i32 @icmp_eq(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_eq:
; CHECK: seq32
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_NE (not equal)
define i32 @icmp_ne(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_ne:
; CHECK: seq32
; The icmp ne branch lowers to seq32 + bnez (branch to else when equal).
; The old bare-xor32 expectation was matching the epilogue r0 re-zero xor
; (the prologue xor precedes seq32 and cannot satisfy it), not a value op;
; F24 removed the epilogue xor in leaf no-call functions, so only seq32 is
; load-bearing here.
entry:
  %cmp = icmp ne i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_SLT (signed less than)
define i32 @icmp_slt(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_slt:
; CHECK: slt32
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_SGT (signed greater than)
define i32 @icmp_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_sgt:
; CHECK: slt32
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_SLE (signed less or equal)
define i32 @icmp_sle(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_sle:
; CHECK: slt32
entry:
  %cmp = icmp sle i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_SGE (signed greater or equal)
define i32 @icmp_sge(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_sge:
; CHECK: slt32
entry:
  %cmp = icmp sge i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_ULT (unsigned less than)
define i32 @icmp_ult(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_ult:
; CHECK: sltu32
entry:
  %cmp = icmp ult i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_UGT (unsigned greater than)
define i32 @icmp_ugt(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_ugt:
; CHECK: sltu32
entry:
  %cmp = icmp ugt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_ULE (unsigned less or equal)
define i32 @icmp_ule(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_ule:
; CHECK: sltu32
entry:
  %cmp = icmp ule i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;ICMP_UGE (unsigned greater or equal)
define i32 @icmp_uge(i32 %a, i32 %b) {
; CHECK-LABEL: icmp_uge:
; CHECK: sltu32
entry:
  %cmp = icmp uge i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;Comparison with zero
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK: seq32
entry:
  %cmp = icmp eq i32 %a, 0
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;Comparison with immediate
define i32 @cmp_imm(i32 %a) {
; CHECK-LABEL: cmp_imm:
; CHECK: slt32
entry:
  %cmp = icmp sgt i32 %a, 42
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;Chained comparisons
define i32 @chained_cmp(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: chained_cmp:
; if (a < b && b < c) return 1 else return 0
; CHECK: slt32
entry:
  %cmp1 = icmp slt i32 %a, %b
  br i1 %cmp1, label %check2, label %else
check2:
  %cmp2 = icmp slt i32 %b, %c
  br i1 %cmp2, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;Branch on compare result (select pattern)
define i32 @branch_cmp_select(i32 %a, i32 %b) {
; CHECK-LABEL: branch_cmp_select:
; return a > b ? a : b
; CHECK: max32
  %cmp = icmp sgt i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;s64 comparison (signed)
; ISA-27 (32-bit compare SFR-decouple) packs the s64 compare's seq32/slt32 into
; denser bundles, so the rigid CHECK order no longer holds. Use CHECK-DAG so the
; test only asserts which compare/logical instructions are emitted, not their
; bundle ordering. Same instructions as before; rebaselined.
define i32 @icmp_s64_slt(i64 %a, i64 %b) {
; CHECK-LABEL: icmp_s64_slt:
; s64 comparison requires comparing hi and lo parts
; CHECK-DAG: slt32
; CHECK-DAG: seq32
; CHECK-DAG: and32
; CHECK-DAG: or32
entry:
  %cmp = icmp slt i64 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;s64 comparison (unsigned)
define i32 @icmp_s64_ugt(i64 %a, i64 %b) {
; CHECK-LABEL: icmp_s64_ugt:
; CHECK-DAG: sltu32
; CHECK-DAG: seq32
; CHECK: and32
; CHECK: or32
entry:
  %cmp = icmp ugt i64 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

;s64 equality
; ISA-27 (32-bit compare SFR-decouple): the two seq32 (hi/lo equality) now pack
; into denser bundles, so rigid CHECK order breaks. CHECK-DAG asserts both seq32
; and the combining and32 are emitted regardless of bundle order.
; Rebaselined.
define i32 @icmp_s64_eq(i64 %a, i64 %b) {
; CHECK-LABEL: icmp_s64_eq:
; CHECK-DAG: seq32
; CHECK-DAG: seq32
; CHECK-DAG: and32
entry:
  %cmp = icmp eq i64 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}
