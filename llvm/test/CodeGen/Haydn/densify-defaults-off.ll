; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=PIPE
; RUN: llc -O2 -mtriple=haydn-unknown-elf -disable-verify -haydn-enable-ldst-opt \
; RUN:     -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | grep -v 'Verify generated machine code' \
; RUN:   | FileCheck %s --check-prefix=LDST-ON
; RUN: llc -O2 -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -debug-only=haydn-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ROLEB-OFF
; RUN: llc -O2 -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -haydn-hwloop-role-b -debug-only=haydn-hwloops < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ROLEB-ON
; RUN: llc -O2 -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -enable-pipeliner=false \
; RUN:     -debug-only=haydn-post-pipeliner,haydn-machine-scheduler < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PP-OFF --allow-empty
; RUN: llc -O2 -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -debug-only=haydn-interblock < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=IB-OFF --allow-empty
;
; REQUIRES: asserts
;
; W0.1 densify product defaults OFF (contracts/pipeline.md / G-RISK-FLAGS).
; Pipeline Structure dump only sees LoadStoreOpt as a pass; IB/PP gate inside
; PostRA (not Structure lines); Role B is a flag on HaydnHardwareLoops.
;
; Locked flags (all cl::init(false) product):
;   -haydn-enable-ldst-opt         PIPE-NOT Load/Store; contrast LDST-ON
;   -haydn-hwloop-role-b           ROLEB-OFF residual=off; ON flips residual=on
;   -haydn-enable-post-pipeliner   PP-OFF no Success / took-region under default
;   -haydn-enable-interblock       IB-OFF no Stage-0 motion debug under default
;
; Behavioral ON fixtures (keep separate; do not enable densify here):
;   post-pipeliner-stage0.ll, interblock-fallthrough-pack.mir,
;   hwloops-extended.ll (-haydn-hwloop-role-b).

; PIPE-NOT:      Haydn Load/Store Optimizer
; PIPE:      Haydn early post-increment pseudo expansion
; PIPE:      Haydn Hardware Loop Detection
; PIPE:      PostRA Machine Instruction Scheduler
; PIPE-NOT:      Haydn Circular Buffer Detection
; PIPE-NOT:      Haydn Redundant Copy Elimination

; LDST-ON: Haydn Load/Store Optimizer
; LDST-ON: Haydn early post-increment pseudo expansion

; ROLEB-OFF: residual=off
; ROLEB-ON: residual=on

; PP-OFF-NOT: PostPipeliner took region
; PP-OFF-NOT: HaydnPostPipeliner: Success

; IB-OFF-NOT: haydn-interblock: fallthrough pack
; IB-OFF-NOT: haydn-interblock: move

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
