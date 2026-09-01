; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-postra-waw-edges -haydn-postra-region-end-edges \
; RUN:     -haydn-postra-interblock < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; P17(a): MaxLatencyFinder + isSimplifiableReservedReg stay default-off
; (RegionEndEdges / WAWEdges / IncludeStages first brick). Force-ON must
; not crash or sequentialize independent ALU. P17(c): SWPSolver is Z3;
; Haydn does not ship LLVM_WITH_Z3, so the seat is fail-closed
; (swpsolver=unavailable). 2026-08-22 SMS product-default flip rebaseline (default ON).
;
; ASM-LABEL: p17_mutations:
; ASM: jalr
; RMK: swpsolver=unavailable
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK: product-on
; RMK-NOT: sequential (preflight)

define i32 @p17_mutations(ptr nocapture readonly %a, ptr nocapture readonly %b,
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
  %add = add i32 %s, %t0
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
