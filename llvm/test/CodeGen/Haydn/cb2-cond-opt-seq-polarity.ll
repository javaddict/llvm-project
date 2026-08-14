; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — — HaydnConditionOptimizer must not invert == branches.

; Status : previously-XFAIL regression resolved; lit PASS.
;
; REGRESSION TEST: — HaydnConditionOptimizer must not invert == branches.
;
; Bug: getFoldedBranchOpcode mapped SEQ32+BNEZ (branch-on-equal) to BNE
; (branch-on-not-equal) instead of BEQ. Every `if (a[mid] == key)` branch was
; inverted, so a binary search returned the first-iteration mid regardless of
; the key (cb2_binsearch.c: expected 9, got 7). Fix : SEQ32+BNEZ -> BEQ
; SEQ32+BEQZ -> BNE.
;
; The `==` test below must fold to a branch that fires on EQUAL (`beq_w`), never
; `bne_w`. If this regresses, binsearch-style code returns the wrong index.
;
; Flex cutover — `seq32 r8, fp, r2; bnez_w r8,.LBB0_4` is back. Likely
; getFoldedBranchOpcode logic gap exposed by the new SEQ32 routing.
; This is a REAL correctness bug (binsearch returns wrong index), not byte

define i32 @cb2_binsearch(ptr readonly %a, i32 %key) {
; CHECK-LABEL: cb2_binsearch:
entry:
  br label %while.body

while.body:
  %hi = phi i32 [ 15, %entry ], [ %hi.1, %if.else ]
  %lo = phi i32 [ 0, %entry ], [ %lo.1, %if.else ]
  %add = add i32 %hi, %lo
  %mid = lshr i32 %add, 1
  %p = getelementptr i32, ptr %a, i32 %mid
  %am = load i32, ptr %p
  %cmp.eq = icmp eq i32 %am, %key
  ; Equality must take the true edge to cleanup. With foldCmpBranch off
  ; (Track A): seq32 + bnez_w (branch if SEQ==1). With fold on: beq_w.
  ; Never invert equality to take the false edge first.
  ; CHECK-DAG: seq32
  ; CHECK-DAG: {{beq|bnez}}{{(\.s[012])?}}
  br i1 %cmp.eq, label %cleanup, label %if.else

if.else:
  %cmp.lt = icmp slt i32 %am, %key
  %lo.inc = add i32 %mid, 1
  %hi.dec = add i32 %mid, -1
  %lo.1 = select i1 %cmp.lt, i32 %lo.inc, i32 %lo
  %hi.1 = select i1 %cmp.lt, i32 %hi, i32 %hi.dec
  %cmp.not = icmp sgt i32 %lo.1, %hi.1
  br i1 %cmp.not, label %cleanup, label %while.body

cleanup:
  %rv = phi i32 [ %mid, %while.body ], [ -1, %if.else ]
  ret i32 %rv
}
