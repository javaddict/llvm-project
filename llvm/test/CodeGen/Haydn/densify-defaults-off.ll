; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; REQUIRES: asserts

; Role: semantic — YOLO phase-out: densify / zombie passes deleted from pipeline (not merely default-OFF).

; YOLO phase-out: densify / zombie passes deleted from pipeline (not merely
; default-OFF). Structure must never list Load/Store, CircularBuffer,
; RedundantCopyElim. Product post-inc expand remains.
;
; Deleted densify (no flags): PostPipeliner, InterBlock, Role B, formMACs,
; LoadStoreOpt form/phase2.

; PIPE-NOT:      Haydn Load/Store Optimizer
; PIPE:      Haydn early post-increment pseudo expansion
; PIPE-NOT:      Haydn Hardware Loop Detection
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE-NOT:      Haydn Circular Buffer Detection
; PIPE-NOT:      Haydn Redundant Copy Elimination

define i32 @sum_loop(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %inext, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s1, %loop ]
  %q = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %q, align 4
  %s1 = add i32 %s, %v
  %inext = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %inext, %n
  br i1 %cond, label %exit.loopexit, label %loop
exit.loopexit:
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s1, %exit.loopexit ]
  ret i32 %r
}
