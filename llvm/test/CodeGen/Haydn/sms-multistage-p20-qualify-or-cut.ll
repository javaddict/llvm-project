; 2026-08-22 SMS product-default flip rebaseline: default is now ON, so the
; OFF arm pins the explicit flag-off contract (-haydn-enable-multistage-sms=0).
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms=0 \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --allow-empty --check-prefix=OFF < %t.off.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.on.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=ON < %t.on.rmk
;
; Qualify-or-cut seat. 2026-08-22 SMS product-default flip: default is ON
; (qualified independent then combined); this file pins the explicit-ON
; remark contract and the flag-OFF silence. The host stays seated (not
; pruned). SWPS is observe-only (no #<swps> stamp from analysis-only).
; SWPSolver is unavailable without Z3 / pragma-II. Combined hwloop+SMS
; stays off. RegionEnd/WAW mutations stay default-off.
;
; ASM-LABEL: p20_qualify:
; ASM-NOT: #<swps>
; ASM: jalr
; OFF-NOT: accepted II=
; OFF-NOT: MultiStageStageMBB
; OFF-NOT: qualify-or-cut
; OFF-NOT: #<swps>
; ON: {{accepted II=|exhausted:|rejected:}}
; ON: qualify-or-cut: seated product-on host-live
; ON: swpsolver=unavailable
; ON: hwloop-combined=off
; ON: nat-ipc=measured-miss
; ON: no-competitive-ipc
; ON: no-stage0-ib-pp
; ON-NOT: sequential (preflight)
; ON-NOT: #<swps>

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
