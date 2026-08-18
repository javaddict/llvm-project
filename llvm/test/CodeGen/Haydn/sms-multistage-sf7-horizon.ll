; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; First-iteration scoreboard horizon is min(II+PD, Size-PD). A multi-cycle
; itinerary fail-closes (product itineraries are single-cycle today).
; Product default stays OFF.
;
; ASM-LABEL: sf7_horizon:
; ASM: jalr
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK-NOT: sequential (preflight)

define i32 @sf7_horizon(ptr nocapture readonly %a, i32 %n) {
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
  %t0 = add i32 %v, 1
  %add = add i32 %s, %t0
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
