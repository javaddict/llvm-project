; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=DUAL
; REQUIRES: asserts

; Role: semantic — product defaults keep hardware-loop formation and
; multi-stage SMS OFF. Densify / zombie passes stay deleted (not merely
; default-OFF). Dual-ON is flag-forced evidence only: Role-A insert/expand/
; fixup appear, deleted densify stays absent, multi-stage remains inside
; PostRA (no extra Structure pass, no product default flip).

; YOLO phase-out: densify / zombie passes deleted from pipeline (not merely
; default-OFF). Structure must never list Load/Store, CircularBuffer,
; RedundantCopyElim. Product post-inc form is ISel.
;
; Deleted densify (no flags): PostPipeliner, InterBlock, Role B, formMACs,
; LoadStoreOpt form/phase2.

; PIPE-NOT:      Hardware Loop Insertion
; PIPE-NOT:      Haydn Load/Store Optimizer
; PIPE-NOT:      Haydn early post-increment pseudo expansion
; PIPE:      Haydn pseudo instruction expansion pass
; Product defaults: hardware loops and multi-stage SMS stay OFF.
; PIPE-NOT:      Haydn Hardware Loop Detection
; PIPE-NOT:      Haydn Hardware Loop Expansion
; PIPE-NOT:      Haydn Hardware Loop Fixup
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE-NOT:      Haydn Circular Buffer Detection
; PIPE-NOT:      Haydn Redundant Copy Elimination

; Dual-ON force: IR insertion + MIR expand + late fixup appear; densify
; remains deleted; multi-stage is not a separate Structure pass.
; DUAL:      Hardware Loop Insertion
; DUAL:      Haydn Hardware Loop Expansion
; DUAL:      PostRA Machine Instruction Scheduler
; DUAL:      Haydn Hardware Loop Fixup
; DUAL-NOT:      Haydn Load/Store Optimizer
; DUAL-NOT:      Haydn Circular Buffer Detection
; DUAL-NOT:      Haydn Redundant Copy Elimination
; DUAL-NOT:      Haydn PostPipeliner
; DUAL-NOT:      Haydn InterBlock

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
