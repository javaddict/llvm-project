; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=LCD < %t.rmk
;
; REGRESSION TEST: two-copy LCD engine must feed RecMII as placement windows.
;
; Bug: RecMII was a post-hoc certificate on a one-copy DAG (or unwired RecMII=0).
; Fix: two-copy buildSchedGraph plus NodeInfo Earliest/Latest/LCDLatest windows.
; Product default remains OFF; hardware loops OFF.
;
; Test design: countdown IV plus mul/add of a carried sum. If the two-copy
; graph or LCD-as-windows path is dropped, RecMII=0 / edges=0 / missing
; lcd-as-windows fails this FileCheck. ASM still emits a legal epilogue
; (analysis-only).
;
; ASM-LABEL: lcd_recmii_sum:
; ASM: jalr
; LCD: lcd two-iteration RecMII={{[1-9][0-9]*}} edges={{[1-9][0-9]*}}
; LCD: lcd-as-windows

define i32 @lcd_recmii_sum(ptr nocapture readonly %a, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %s = phi i32 [ 0, %pre ], [ %add, %body ]
  %inext = add nsw i32 %i, -1
  %p = getelementptr inbounds i32, ptr %a, i32 %inext
  %v = load i32, ptr %p, align 4
  %t0 = mul i32 %s, 3
  %add = add i32 %t0, %v
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
