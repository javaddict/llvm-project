; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS
; REQUIRES: asserts

; Role: semantic — multi-BB KPI measurement seat under HWON.
; Innermost latch-only diamonds accept as Role A. Early-exit, nested
; outer, and multi-latch stay declined (measured residual). Single-BB
; Role-A accept stays non-zero. Zero counters omit from -stats.

target triple = "haydn-unknown-elf"

; Side-effect stores in both arms resist if-conversion → latch-only diamond
; accept (Role A). Decline counters come from early-exit / nested-outer /
; multi-latch below.
define void @kpi_multibb_decline(ptr %dst, ptr readonly %src, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else
then:
  store i32 0, ptr %dp, align 4
  br label %latch
else:
  store i32 %v, ptr %dp, align 4
  br label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; Single-BB Role-A accept companion so the KPI seat also records accepted
; single-BB candidates under the same HWON run (differential signal).
define i32 @kpi_singlebb_accept(ptr readonly %p, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}

; Early-exit multi-BB stays declined (latch is not the unique exiting block).
define i32 @kpi_multibb_early_exit(ptr readonly %src, i32 %n, i32 %k) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %v = load i32, ptr %sp, align 4
  %hit = icmp eq i32 %v, %k
  br i1 %hit, label %early, label %latch
latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
early:
  ret i32 %v
exit:
  ret i32 0
}

; Nested outer multi-BB is declined (not innermost). Inner single-BB may
; accept under the same HWON run (Role-A accept stays non-zero).
define void @kpi_nested_outer_decline(ptr %p, i32 %n, i32 %m) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %outer, label %exit
outer:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  br label %inner
inner:
  %j = phi i32 [ 0, %outer ], [ %j.next, %inner ]
  store i32 %j, ptr %p, align 4
  %j.next = add i32 %j, 1
  %id = icmp slt i32 %j.next, %m
  br i1 %id, label %inner, label %outer.latch
outer.latch:
  %i.next = add i32 %i, 1
  %od = icmp slt i32 %i.next, %n
  br i1 %od, label %outer, label %exit
exit:
  ret void
}

; Two back-edges: no unique latch. Declined (never Role-B rediscovery).
define void @kpi_multilatch_decline(ptr %p, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %header, label %exit
header:
  %i = phi i32 [ 0, %entry ], [ %i.a, %latch.a ], [ %i.b, %latch.b ]
  %v = load i32, ptr %p, align 4
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %latch.a, label %latch.b
latch.a:
  store i32 0, ptr %p, align 4
  %i.a = add i32 %i, 1
  %da = icmp slt i32 %i.a, %n
  br i1 %da, label %header, label %exit
latch.b:
  store i32 1, ptr %p, align 4
  %i.b = add i32 %i, 2
  %db = icmp slt i32 %i.b, %n
  br i1 %db, label %header, label %exit
exit:
  ret void
}

; Multi-BB residual decline (early-exit / nested-outer / multi-latch)
; and Role-A accept (single-BB + latch-only diamond) must both be
; non-zero. Latch-only accept is the measured SCEV/CFG extension;
; remaining multi-BB is a measured miss. Never late physical rediscovery.
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}multi-BB loops declined
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}latch-only multi-BB loops accepted
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}Role-A hardware-loop candidates accepted
