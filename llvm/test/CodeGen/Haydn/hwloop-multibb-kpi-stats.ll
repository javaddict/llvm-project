; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS
; REQUIRES: asserts

; Role: semantic — multi-BB KPI measurement seat under HWON.
; Counts multi-BB declines at TTI so product evidence can measure miss after
; Role A ships; never revives multi-BB formation. Zero counters omit from
; -stats, so this kernel must keep a multi-BB side-effect shape.

target triple = "haydn-unknown-elf"

; Side-effect stores in both arms resist if-conversion → multi-BB decline.
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

; Multi-BB declines and Role-A accepts must both be non-zero under HWON.
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}multi-BB loops declined
; STATS-DAG: {{[1-9][0-9]*}} haydn-tti {{.*}}Role-A hardware-loop candidates accepted
