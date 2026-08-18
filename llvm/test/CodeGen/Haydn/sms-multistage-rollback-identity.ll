; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs < %s \
; RUN:   > %t.off
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     < %s > %t.an
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-CFG < %s > %t.pfc
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-PHI < %s > %t.pfp
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-TRIP < %s > %t.pft
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-STAGE < %s > %t.pfs
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-LIVE < %s > %t.pfl
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-ALT < %s > %t.pfa
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-BUNDLE < %s > %t.pfb
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=PF-LATE < %s > %t.pfe
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-ALLOC < %s > %t.jm
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-SPLICE < %s > %t.sp
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-COMMIT < %s > %t.cm
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-TRIP < %s > %t.tr
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-LIVE < %s > %t.lv
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-ALT < %s > %t.al
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-META < %s > %t.mt
; RUN: diff %t.off %t.an
; RUN: diff %t.off %t.pfc
; RUN: diff %t.off %t.pfp
; RUN: diff %t.off %t.pft
; RUN: diff %t.off %t.pfs
; RUN: diff %t.off %t.pfl
; RUN: diff %t.off %t.pfa
; RUN: diff %t.off %t.pfb
; RUN: diff %t.off %t.pfe
; RUN: diff %t.off %t.jm
; RUN: diff %t.off %t.sp
; RUN: diff %t.off %t.cm
; RUN: diff %t.off %t.tr
; RUN: diff %t.off %t.lv
; RUN: diff %t.off %t.al
; RUN: diff %t.off %t.mt
; RUN: FileCheck %s < %t.off
;
; Identity-preserving rollback QUALIFY (hardware loops OFF, one artifact):
; analysis-only, every PF-* preflight force-fail, and every post-mutation
; JM-* force-fail must restore the ordinary scheduled baseline
; byte-for-byte in assembly. Product default stays OFF.
;
; CHECK-LABEL: rollback_sum:
; CHECK: jalr

define i32 @rollback_sum(ptr nocapture readonly %a, i32 %n) {
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
