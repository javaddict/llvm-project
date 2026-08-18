; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -stop-before=haydn-finalize-mi-bundles \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.mat.rmk | FileCheck %s --check-prefix=MAT
; RUN: FileCheck %s --check-prefix=MATRMK < %t.mat.rmk
;
; Cycle-accurate peel emission (visitPipelineSection order) plus epilogue
; scoreboard pre-seed from kernel steady state (AIE initializeTopScoreBoard
; replay, then peel). Analysis-only never mutates. Materialize names the
; pre-seed seat on any accept/rollback path. Product default stays OFF.
;
; ASM-LABEL: sf10_peel:
; ASM: jalr
; RMK: {{accepted II=|exhausted:|rejected:}}
; MAT: name: sf10_peel
; MATRMK: {{peel-order=modulo-cycle|epilogue-preseed=|exhausted:|rejected:|preflight reject:}}
; MATRMK-NOT: sequential (preflight)

define i32 @sf10_peel(ptr nocapture readonly %a, i32 %n) {
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
  %add = xor i32 %t0, 7
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
