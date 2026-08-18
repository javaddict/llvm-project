; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --allow-empty --check-prefix=OFF < %t.off.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.on.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=ON < %t.on.rmk
;
; P20(i) qualify-or-cut seat. Product default stays OFF until independent
; then combined QUALIFY. The host may search under the explicit flag;
; sunset/prune is deferred to that measurement, not a silent product-ON.
; SWPSolver (P17(c)) is unavailable without Z3 / pragma-II. Combined
; hwloop+SMS stays off.
;
; ASM-LABEL: p20_qualify:
; ASM: jalr
; OFF-NOT: accepted II=
; OFF-NOT: MultiStageStageMBB
; OFF-NOT: qualify-or-cut
; ON: {{accepted II=|exhausted:|rejected:}}
; ON: qualify-or-cut
; ON: product-off
; ON: swpsolver=unavailable
; ON: hwloop-combined=off
; ON: nat-ipc=measured-miss
; ON: no-competitive-ipc
; ON: no-stage0-ib-pp
; ON-NOT: sequential (preflight)

define i32 @p20_qualify(ptr nocapture readonly %a, i32 %n) {
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
  %t0 = add i32 %s, %v
  %add = add i32 %t0, 3
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
