; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk

; G009 LOOP PRAGMA: infeasible initiationinterval declines, no fallback.
;
; REGRESSION TEST CONTRACT: when llvm.loop.pipeline.initiationinterval N is
; INFEASIBLE for the loop body, the post-RA engine must decline naming the
; pragma (seat=pragma-ii-infeasible) and must NOT fall back to searching
; another II. The user pinned a contract; silently scheduling a different
; II would violate it. AIE discipline: the solver is gated to TargetII and
; simply fails; Haydn records the decline in the G005 canonical vocabulary
; (kind=declined, seat names the pragma). The engine's IIHint relaxation
; loop (host-suggested II) is explicitly bypassed under a pragma pin.
;
; Test design: dot-store kernel whose natural accept is II=5 with PROVEN
; ResMII=3 (baseline accept line below), pinned to II=2 — strictly below
; the resource floor, so no schedule can exist. Expected: kind=declined
; seat=pragma-ii-infeasible. Control arm in-file: same kernel, no pragma,
; accepts (proves the body is schedulable and only the pin declined).
; RMK pins the declined line's kind+seat, the control's accepted kind,
; and nothing after.

target triple = "haydn-unknown-elf"

; Pinned-below-floor loop: declined, seat names the pragma. NS captured:
; a failed tryII may leave a partial stage count (engine-internal, not
; contract; the pinned II=2 IS infeasible so no schedule committed).
; RMK: Schedule found II=[[DII:[0-9]+]] NS=[[DNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=declined seat=pragma-ii-infeasible
; RMK-SAME: loop=bb.{{[0-9]+}}.loop
; Control loop: no pragma — accepts (the body is schedulable; only the
; impossible pin declined).
; RMK: Schedule found II=[[AII:[0-9]+]] NS=[[ANS:[0-9]+]] prologue=
; RMK-SAME: {{[0-9]+}} parcels epilogue={{[0-9]+}} parcels kind=accepted
; RMK-SAME: loop=bb.{{[0-9]+}}.loop
; RMK-NOT: Schedule found

; ASM: pinned_infeasible:
; ASM: jalr
; ASM: control:
; ASM: jalr

define void @pinned_infeasible(ptr readonly %a, ptr readonly %b, ptr %d, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %s.n = add i32 %s, %m
  store i32 %s.n, ptr %d, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}

define void @control(ptr readonly %a, ptr readonly %b, ptr %d, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %s.n = add i32 %s, %m
  store i32 %s.n, ptr %d, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !2
exit:
  ret void
}

!0 = distinct !{!0, !1, !4}
!1 = !{!"llvm.loop.pipeline.initiationinterval", i32 2}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.itercount.range", i32 8}
!4 = !{!"llvm.loop.itercount.range", i32 8}
