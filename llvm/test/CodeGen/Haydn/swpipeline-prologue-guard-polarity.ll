; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s | FileCheck %s
;
;
; REGRESSION TEST: Software-pipeliner prologue guard polarity
;
; Bug (,): HaydnPipelinerLoopInfo::createTripCountGreaterCondition
; (HaydnInstrInfo.cpp) emitted the prologue skip-branch with the wrong
; polarity. The upstream PeelingModuloScheduleExpander contract
; (ModuloSchedule.cpp:886,1975) requires the Cond produced by this hook to be
; TRUE (branch-taken) when the trip count is NOT greater than TC -- i.e. the
; branch should fire only to SKIP the prologue/kernel when the trip is too
; small. The Haydn code computed
; CmpResult = (TripCountReg < TC+1); true when trip <= TC (skip case)
; Cond = [BEQZ, CmpResult]; fires when CmpResult == 0
; i.e. it branched when trip > TC (the EXECUTE case), inverting the guard.
; For any pipelined loop whose trip count exceeded the stage count, the
; prologue "skip" branch fired on entry and dead-stripped the entire loop
; body. (counting sort, trip 16) returned 45 instead of 146;
; (vec-max reduction, trip 31) returned 9 instead of 254.
;
; Fix: use BNEZ so the branch fires when CmpResult != 0 (trip <= TC, the
; genuine skip case), matching Hexagon's J2_jumpf reference implementation.
;
; Test design: a reduction loop over a known array with a software-pipelined
; (non-hwloop) body. With the bug, the reduction is bypassed and the returned
; value equals the initial accumulator (x[0]). With the fix, the reduction
; runs and the result is the true max.
;
; We cannot assert the numeric result at the.s level (the array contents are
; computed at runtime), so we instead assert the structural property: the
; prologue guard must branch on the "skip when trip is small" condition. The
; pre-fix bug emitted `beqz_w` immediately after the slt32 guard; the fix emits
; `bnez_w`. We also confirm the loop body is reached (not dead-stripped) by
; checking for the max32 reduction instruction inside the loop.
;
; STALE-FAILMARKER REMOVED (, post- cutover): the 2026-07
; regression cleared — SMS now fires on this loop again (Schedule Found? 1
; II=1) and the slt32 + bnez_w prologue guard is present in the output. The
; BNEZ polarity fix in HaydnInstrInfo.cpp is once again covered.

; Reduction: max over 16 ints. The pipeliner prologue guard must keep the
; loop body reachable. With the polarity bug, the whole body is skipped.
define dso_local i32 @test_prologue_guard_polarity() local_unnamed_addr #0 {
entry:
  %buf = alloca [16 x i32], align 4
  store i32 1, ptr %buf, align 4
  %p1 = getelementptr inbounds nuw i8, ptr %buf, i32 4
  store i32 5, ptr %p1, align 4
  %p2 = getelementptr inbounds nuw i8, ptr %buf, i32 8
  store i32 2, ptr %p2, align 4
  %p3 = getelementptr inbounds nuw i8, ptr %buf, i32 12
  store i32 8, ptr %p3, align 4
  %p4 = getelementptr inbounds nuw i8, ptr %buf, i32 16
  store i32 3, ptr %p4, align 4
  %p5 = getelementptr inbounds nuw i8, ptr %buf, i32 20
  store i32 9, ptr %p5, align 4
  %p6 = getelementptr inbounds nuw i8, ptr %buf, i32 24
  store i32 4, ptr %p6, align 4
  %p7 = getelementptr inbounds nuw i8, ptr %buf, i32 28
  store i32 7, ptr %p7, align 4
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %mx = phi i32 [ 1, %entry ], [ %sel, %loop ]
  %gep = getelementptr inbounds nuw i32, ptr %buf, i32 %i
  %v = load i32, ptr %gep, align 4
  %cmp = icmp sgt i32 %v, %mx
  %sel = select i1 %cmp, i32 %v, i32 %mx
  %inc = add nuw nsw i32 %i, 1
  %exit = icmp eq i32 %inc, 8
  br i1 %exit, label %exitbb, label %loop

exitbb:
  ret i32 %sel
}

; The prologue guard emitted by createTripCountGreaterCondition uses BNEZ
; on the (trip <= TC) comparison result. The pre-fix code emitted BEQZ.
;
; NOTE: post- Flex cutover, ALU32 ops carry a slot suffix (//) or
; the legacy _m0 variant — the regex below accepts either form.
;
; CHECK-LABEL: test_prologue_guard_polarity:
; CHECK: slt32{{(_m0|\.s[0-9])?}} {{r[0-9]+}}, {{r[0-9]+}}, {{r[0-9]+}}
; CHECK-NOT: beqz_w{{(\.s[012])?}}
; CHECK: bnez_w{{(\.s[012])?}} {{r[0-9]+}}
;
; The reduction body must be present (not dead-stripped by the inverted
; guard). max32 is the reduction operator.
; CHECK: max32{{(_m0|\.s[0-9])?}}

attributes #0 = { nofree norecurse nosync nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
