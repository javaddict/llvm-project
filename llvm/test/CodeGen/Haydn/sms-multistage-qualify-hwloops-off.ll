; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: FileCheck %s --check-prefix=ACC < %t.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -stop-before=haydn-finalize-mi-bundles \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.mat.rmk | FileCheck %s --check-prefix=MAT
; RUN: FileCheck %s --check-prefix=MATRMK < %t.mat.rmk
;
; Same-artifact QUALIFY with hardware loops OFF. Realized parcels-per-iter
; must equal searched II on every accepted kernel (SF3). Resource-bias
; windows (SF9) and epilogue scoreboard pre-seed (SF10) are named. SWPS
; is observe-only. 2026-08-22 SMS product-default flip rebaseline (default ON).
;
; ASM-LABEL: qualify_hwloops_off:
; ASM: jalr
; RMK-NOT: sequential (preflight)
; RMK-DAG: resource-bias=slot-windows
; RMK-DAG: no-seq-fallback
; RMK-DAG: accepted II=
; RMK-DAG: qualify-or-cut
; RMK-DAG: product-on
; RMK-DAG: swpsolver=unavailable
; RMK-DAG: hwloop-combined=off
; RMK-DAG: nat-ipc=measured-miss
; RMK-DAG: no-competitive-ipc
; RMK-DAG: no-stage0-ib-pp
;
; ACC: accepted II=[[II:[0-9]+]]
; ACC-SAME: measured-II=[[II]]
; ACC: qualify parcels-per-iter=[[II]]
; ACC-SAME: searched-II=[[II]]
; ACC: swps observe-only measured-II=[[II]]
; ACC-SAME: searched-II=[[II]] no-asm-stamp
;
; MAT: name: qualify_hwloops_off
; MATRMK-NOT: sequential (preflight)
; MATRMK: accepted II=
; MATRMK: qualify-or-cut
; MATRMK: product-on
; MATRMK: {{peel-order=modulo-cycle|epilogue-preseed=}}

define i32 @qualify_hwloops_off(ptr nocapture readonly %a,
                                ptr nocapture readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %x = phi i32 [ 0, %pre ], [ %x2, %body ]
  %y = phi i32 [ 0, %pre ], [ %y2, %body ]
  %inext = add nsw i32 %i, -1
  %x2 = add i32 %x, 5
  %y2 = add i32 %y, 7
  %v0 = load i32, ptr %a, align 4
  %v1 = load i32, ptr %b, align 4
  %t1 = add i32 %v0, %x2
  %z2 = add i32 %t1, %y2
  %t2 = add i32 %z2, %v1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %w = phi i32 [ 0, %entry ], [ %t2, %body ]
  ret i32 %w
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
