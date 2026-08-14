; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only < %s | FileCheck %s --check-prefix=ANALYSIS
; RUN: llc -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-force-fail-seat=PF-CFG < %s | FileCheck %s --check-prefix=FORCE
; OFF-LABEL: add_loop:
; OFF: jalr
; ANALYSIS-LABEL: add_loop:
; ANALYSIS: jalr
; FORCE-LABEL: add_loop:
; FORCE: jalr
define i32 @add_loop(ptr nocapture readonly %a, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %for.body.preheader, label %exit
for.body.preheader:
  br label %for.body
exit.loopexit:
  %sum.lcssa = phi i32 [ %add, %for.body ]
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %sum.lcssa, %exit.loopexit ]
  ret i32 %r
for.body:
  %i = phi i32 [ %i.next, %for.body ], [ 0, %for.body.preheader ]
  %sum = phi i32 [ %add, %for.body ], [ 0, %for.body.preheader ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p, align 4
  %add = add i32 %sum, %v
  %i.next = add nuw nsw i32 %i, 1
  %cond = icmp eq i32 %i.next, %n
  br i1 %cond, label %exit.loopexit, label %for.body
}
