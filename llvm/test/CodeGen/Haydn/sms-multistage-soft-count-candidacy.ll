; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only < %s \
; RUN:     | FileCheck %s --check-prefix=SOFT
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-CFG < %s \
; RUN:     | FileCheck %s --check-prefix=FORCE
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-TRIP < %s \
; RUN:     | FileCheck %s --check-prefix=FORCE
;
; Soft-counted candidacy (hardware loops OFF): multi-stage host may enter on a
; single-BB self-loop with a body countdown ADDI/SUBI. Analysis-only and PF/JM
; force-fail seats must retain ordinary baseline and still emit a legal epilogue.
;
; SOFT-LABEL: soft_count_sum:
; SOFT: jalr
; FORCE-LABEL: soft_count_sum:
; FORCE: jalr
define i32 @soft_count_sum(ptr nocapture readonly %a, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
exit.loopexit:
  %s.lcssa = phi i32 [ %add, %body ]
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.lcssa, %exit.loopexit ]
  ret i32 %r
body:
  %i = phi i32 [ 0, %pre ], [ %inext, %body ]
  %s = phi i32 [ 0, %pre ], [ %add, %body ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p, align 4
  %add = add i32 %s, %v
  %inext = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %inext, %n
  br i1 %cond, label %exit.loopexit, label %body
}
