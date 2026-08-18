; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; SF9 resource-bias windows: conflict-set SlotCounts on each node, local
; predecessor contention (AIE biasForLocalResourceContention), and
; ancestor/offspring slot-count folds. The pin remark always names the
; seat; accept/exhaust is fail-closed. Product default stays OFF.
;
; ASM-LABEL: sf9_bias:
; ASM: jalr
; RMK: resource-bias=slot-windows
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK-NOT: sequential (preflight)

define i32 @sf9_bias(ptr nocapture readonly %a, ptr nocapture readonly %b,
                     i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %s = phi i32 [ 0, %pre ], [ %add, %body ]
  %inext = add nsw i32 %i, -1
  %pa = getelementptr inbounds i32, ptr %a, i32 %inext
  %pb = getelementptr inbounds i32, ptr %b, i32 %inext
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %t0 = add i32 %va, %vb
  %t1 = add i32 %s, %t0
  %add = xor i32 %t1, 3
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
