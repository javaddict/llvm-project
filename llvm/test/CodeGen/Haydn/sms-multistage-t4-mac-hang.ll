; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -stop-before=haydn-finalize-mi-bundles \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.mat.rmk | FileCheck %s --check-prefix=MAT
; RUN: FileCheck %s --check-prefix=MATRMK < %t.mat.rmk
;
; T4 hang-root was Latest-to--inf and LastEarliestPusher cycles on dense
; MAC DAGs (bkfir). Those walks are capped; this smaller MAC body runs
; through RA + post-RA so QUALIFY is not stuck behind the generic
; pipeliner stop-after. Product default stays OFF.
;
; ASM-LABEL: t4_mac:
; ASM: jalr
; RMK: resource-bias=slot-windows
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK: qualify-or-cut
; RMK: product-off
; RMK-NOT: sequential (preflight)
; MAT: name: t4_mac
; MATRMK: {{accepted II=|exhausted:|rejected:|preflight reject:}}
; MATRMK: product-off

define i32 @t4_mac(ptr nocapture readonly %a, ptr nocapture readonly %b,
                   i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %body
body:
  %i = phi i32 [ %n, %pre ], [ %inext, %body ]
  %acc = phi i32 [ 0, %pre ], [ %acc.n, %body ]
  %inext = add nsw i32 %i, -1
  %pa = getelementptr inbounds i32, ptr %a, i32 %inext
  %pb = getelementptr inbounds i32, ptr %b, i32 %inext
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m0 = mul i32 %va, %vb
  %acc.a = add i32 %acc, %m0
  %m1 = mul i32 %va, %acc.a
  %acc.n = add i32 %acc.a, %m1
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %acc.n, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
