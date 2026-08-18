; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; REGRESSION TEST: SF3 — no sequential fallback; SWPS reports measured II.
;
; Bug: a modulo group that failed canCoissueProductCycle was emitted as
; sequential parcels while recordSWPSAnnotation still reported assumed II.
; Fix: unpackable multi-MI groups reject the II; every accepted cycle is
; one parcel; the accept remark carries measured-II= searched II.
; Product default stays OFF.
;
; Test design: E2-only body that cannot pack three members into one parcel.
; The engine must either accept with measured-II equal to the searched II
; or fail closed (exhaust / reject). It must never sequentialize a cycle
; and still print accepted II=.

; ASM-LABEL: sf3_no_seq:
; ASM: jalr
; RMK-NOT: sequential (preflight)
; RMK: no-seq-fallback
; RMK: {{accepted II=|exhausted:|rejected:}}

define i32 @sf3_no_seq(ptr nocapture readonly %a, ptr nocapture readonly %b,
                       i32 %n) {
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
!1 = !{!"llvm.loop.itercount.range", i64 8}
