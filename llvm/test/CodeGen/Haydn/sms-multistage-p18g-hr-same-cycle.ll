; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; P18(g): multi-stage placement applies the three HR same-cycle laws
; (ARCTAN/SIN_COS alone, CSRW↔SET_HWLOOP, LUI/ADDI32_W e0-alone).
; Those flags live on the HR emit path, not checkConflict; the host
; conjuncts them at fitInInterval. Product default stays OFF.
;
; ASM-LABEL: p18g_hr_laws:
; ASM: jalr
; RMK: hr-same-cycle=arctan-sincos+csrw-set+abs-e0
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK-NOT: sequential (preflight)

define i32 @p18g_hr_laws(ptr nocapture readonly %a, i32 %n) {
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
  %t0 = xor i32 %s, %v
  %add = add i32 %t0, 5
  %cond = icmp eq i32 %inext, 0
  br i1 %cond, label %exit, label %body, !llvm.loop !0
exit:
  %r = phi i32 [ 0, %entry ], [ %add, %body ]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i64 16}
