; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: FileCheck %s --check-prefix=ACC < %t.rmk
;
; REGRESSION TEST: G002 II-parity — realized parcels-per-iteration must
; equal the searched II on every accepted kernel.
;
; Bug: the SF3 "MeasuredII==II" certificate counted the PLANNED
; ExactCommitPlan modulo cycles (ParcelsCommitted = II by construction),
; never the realized post-commit stream. Two real parcel consumers were
; invisible to the II search:
;   1. the Form-C latch branch (BNEZ on the compare chain) is a scheduling
;      boundary, so it never entered the DAG/body — it landed after the
;      kernel as its own parcel (+1 on every accepted loop);
;   2. dest-window stall pads the realized stream incurs.
; The AsmPrinter AchievedII stamp (the honest counter) reported 4/6 vs
; remark II=3/4 with verdict=schedule-limited — the remark lied.
;
; Fix (HaydnPostRAMultiStage.cpp): the latch branch rides the two-copy
; graph as an ordinary node (AIE isPostPipelineCandidate peer) pinned to
; modulo cycle II-1 — the kernel's final parcel carries the terminator, so
; realized==II by construction; the commit tail recounts REALIZED parcels
; (Haydn::countKernelIssueParcels — the same counter the #<swps> stamp
; uses — plus the LatencyStalls dest-window replay) and fails closed
; (rollback, no seq fallback) on any mismatch.
;
; Test design: soft-countdown loop whose steady state is a compare chain
; (seq -> xori -> bnez). Pre-fix the kernel realized 4 parcels with the
; remark claiming II=3 (AchievedII=4, verdict=schedule-limited). Post-fix
; the branch coissues into the kernel's final parcel: AchievedII equals
; the scheduled II and the verdict line is not schedule-limited. If the
; honest certificate ever disagrees, the loop must REJECT (exhausted:),
; never accept with a lying II.

; RMK-NOT: sequential (preflight)
; RMK-DAG: accepted II=
; RMK-DAG: no-seq-fallback

; ACC: accepted II=[[II:[0-9]+]]
; ACC-SAME: measured-II=[[II]]
; ACC: qualify parcels-per-iter=[[II]]
; ACC-SAME: searched-II=[[II]]
; ACC: swps observe-only measured-II=[[II]]

; The parity law itself: the scheduled II and the realized kernel parcel
; count (AsmPrinter AchievedII = the same shared counter the qualify
; certificate uses) are EQUAL. verdict may legitimately read
; schedule-limited (II can exceed ResMII because the branch budget prices a
; real slot) — the defect was AchievedII != II, never the verdict label.
; ASM-LABEL: qualify_hwloops_off:
; ASM: #<swps> II=[[II:[0-9]+]] cycles per pipeline stage (SMS schedule)
; ASM: #<swps> AchievedII=[[II]] (kernel parcels)
; ASM: jalr

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
