; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — — a loop whose body branches OUT to the loop exit (an early `break` / mid-body exit) must NOT be lowered to a hardware.

; REGRESSION TEST: / — a loop whose body branches OUT to the loop
; exit (an early `break` / mid-body exit) must NOT be lowered to a hardware
; loop (ZOL). ISA rule : a ZOL body may branch internally, but branches
; must NOT jump out of the loop region — exiting while HWLR is active
; corrupts HWLR_COUNT, so the loop runs once instead of N (exp 3 -> sim 1).
;
; Fix: HaydnHardwareLoops eligibility rejects loops where a non-latch body
; block exits (exit edge only from the latch) -> software loop.

; early_break: a counted loop with a mid-body `if (x == k) break;`. The break
; exits the loop from a non-latch block -> must NOT form a hwloop.

define i32 @early_break(ptr %a, i32 %n, i32 %k) {
; CHECK-LABEL: early_break:
; CHECK-NOT: set_hwloop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %cont ]
  %p = getelementptr i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  %found = icmp eq i32 %v, %k
  br i1 %found, label %exit, label %cont
cont:
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  %r = phi i32 [ %i, %loop ], [ -1, %cont ]
  ret i32 %r
}

; counted_loop: a genuine counted loop (no early exit, single back-edge from
; the latch). MUST still form a hwloop — guard against over-rejecting.
define void @counted_loop(ptr %a, i32 %n) {
; CHECK-LABEL: counted_loop:
; CHECK: set_hwloop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %p = getelementptr i32, ptr %a, i32 %i
  store i32 %i, ptr %p
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}
