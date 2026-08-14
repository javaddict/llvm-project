; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-force-fail-seat=PF-PHI < %s | FileCheck %s
; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-force-fail-seat=JM-ALLOC < %s | FileCheck %s
; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-force-fail < %s | FileCheck %s
; CHECK-LABEL: force_fail_loop:
; CHECK: jalr
define i32 @force_fail_loop(ptr nocapture readonly %a, i32 %n) {
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
