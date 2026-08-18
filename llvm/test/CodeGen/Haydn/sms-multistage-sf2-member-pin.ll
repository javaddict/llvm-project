; RUN: llc -global-isel-abort=1 -mtriple=haydn -mattr=-hwloop -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-multistage-sms -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; REGRESSION TEST: SF2 — transient member pin before placement.
;
; Bug: MultiSlot logicals entered the format oracle as logicals, so unit /
; entry geometry was not the generated member the commit tail re-solves.
; Fix: preferredMemberOpcode pin (OpcodePin) before tryII; PF-ALT verifies
; every tracked body logical is pinned. Product default stays OFF.
;
; Test design: a soft-count body with MultiSlot ALU/LS logicals. The accept
; or exhaust remark must carry pins=N (N>0) when the engine reaches II
; search. A missing pin fails closed at PF-ALT / member-pin — never a
; sequential kernel.

; ASM-LABEL: sf2_member_pin:
; ASM: jalr
; RMK: member-pin n={{[0-9]+}}
; RMK: {{accepted II=|exhausted:|rejected:}}
; RMK-NOT: sequential (preflight)

define i32 @sf2_member_pin(ptr nocapture readonly %a, i32 %n) {
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
