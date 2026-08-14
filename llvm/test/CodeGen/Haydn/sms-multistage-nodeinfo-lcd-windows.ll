; RUN: llc -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=WIN < %t.rmk
;
; REGRESSION TEST: NodeInfo LCD windows are placement inputs, not post-hoc
; SMSLCDTwoIteration certificates. Product default remains OFF.
;
; Bug: HaydnMultiStageNodeInfo carried only Earliest. RecMII/LCD were
; certificates after ASAP placement, and memory LCDs were omitted.
;
; Fix: AIE-shaped NodeInfo (Earliest/Latest/LCDLatest/Static/Tweaked) plus
; two-copy buildSchedGraph. Analysis-only must report lcd-as-windows.
; Hardware loops stay OFF; do not combine dual-ON.
;
; Test design: carried accumulator + countdown. If LCDLatest/windows are
; dropped, WIN fails to match lcd-as-windows while RecMII/edges stay >0.

; ASM-LABEL: nodeinfo_lcd_windows:
; ASM: jalr
; WIN: lcd two-iteration RecMII={{[1-9][0-9]*}} edges={{[1-9][0-9]*}}
; WIN: lcd-as-windows
; WIN-NOT: memory LCD unmodeled

define i32 @nodeinfo_lcd_windows(ptr nocapture readonly %a, i32 %n) {
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
  %t0 = mul i32 %s, 5
  %add = add i32 %t0, %v
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
