; Host present, product default OFF. Band 2S: checkConflict books Format E
; Slots and asks isFormatAvailable / getFormatOrNull / productCovers (AIE
; FuncUnitWrapper::conflict). Sequential fallback is not a legal accept;
; SWPS is observe-only unless measured-II equals searched II.
; RUN: llc -global-isel-abort=1 -mtriple=haydn -O2 -verify-machineinstrs \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.an.rmk | FileCheck %s --check-prefix=ANALYSIS
; RUN: FileCheck %s --allow-empty --check-prefix=ANRMK < %t.an.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -O2 -verify-machineinstrs -haydn-enable-multistage-sms -haydn-multistage-sms-force-fail-seat=PF-CFG < %s | FileCheck %s --check-prefix=FORCE
; OFF-LABEL: add_loop:
; OFF: jalr
; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; OFFRMK-NOT: swps measured-II=
; ANALYSIS-LABEL: add_loop:
; ANALYSIS-NOT: #<swps>
; ANALYSIS: jalr
; ANRMK-NOT: Sequential
; ANRMK: qualify-or-cut: seated product-off host-live
; ANRMK: swpsolver=unavailable
; ANRMK: hwloop-combined=off
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
