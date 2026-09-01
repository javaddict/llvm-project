; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: FileCheck %s --check-prefix=COUNT < %t.rmk

; G009 LOOP PRAGMA: llvm.loop.pipeline.disable honored by BOTH engines.
;
; REGRESSION TEST CONTRACT: `llvm.loop.pipeline.disable` on a loop the
; post-RA multi-stage engine would otherwise ACCEPT must veto it. The
; canonical G005 line records kind=not-candidate seat=pragma-disable (the
; user withdrew the loop; its shape was never judged), and no engine
; accept line fires for it. The pre-RA generic MachinePipeliner parses
; the same pragma (setPragmaPipelineOptions) and vetoes in
; canPipelineLoop ("Disabled by Pragma") — zero Haydn-side code there.
; Peer shape: Hexagon swp-pragma-disable.ii. The control arm in this same
; file (identical kernel, itercount MD but NO pipeline pragma) proves the
; pragma — not the kernel — is what declined @vetoed.
;
; Test design: both loops are the G006 dualacc kernel (known acceptor,
; II=4/NS=1) with llvm.loop.itercount.range trip facts. @vetoed
; additionally carries llvm.loop.pipeline.disable. RMK pins the canonical
; not-candidate line WITH the pragma seat, then the control's accept
; line. II values on the vetoed line are captured, not pinned (noise
; rule); the contract is the kind + seat vocabulary and accept absence.

target triple = "haydn-unknown-elf"

; Engine lines fire at the region; canonical lines batch at
; finalizeSchedule. The veto line names the pragma.
; RMK: rejected: llvm.loop.pipeline.disable (user veto)
; Vetoed loop canonical line: not-candidate naming the pragma seat.
; RMK: Schedule found II=[[VII:[0-9]+]] NS=0 prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=not-candidate seat=pragma-disable
; RMK-SAME: loop=bb.{{[0-9]+}}.loop
; Control loop: unchanged accept (engine alive; pragma is the only delta).
; II captured per the G006 noise rule; the kind= field distinguishes the
; two canonical lines (order + kind, not II values, is the contract).
; RMK: Schedule found II=[[AII:[0-9]+]] NS=[[CNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.loop
; Exactly two canonical lines, in this order, none after.
; COUNT-COUNT-2: Schedule found
; COUNT-NOT: Schedule found

; ASM: vetoed:
; ASM: jalr
; ASM: control:
; ASM: jalr

define i32 @vetoed(ptr readonly %a, ptr readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s1 = phi i32 [0, %pre], [%s1.n, %loop]
  %s2 = phi i32 [0, %pre], [%s2.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m1 = mul i32 %va, %vb
  %s1.n = add i32 %s1, %m1
  %m2 = mul i32 %vb, 3
  %s2.n = add i32 %s2, %m2
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [0, %entry], [%s2.n, %loop]
  ret i32 %r
}

define i32 @control(ptr readonly %a, ptr readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s1 = phi i32 [0, %pre], [%s1.n, %loop]
  %s2 = phi i32 [0, %pre], [%s2.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m1 = mul i32 %va, %vb
  %s1.n = add i32 %s1, %m1
  %m2 = mul i32 %vb, 3
  %s2.n = add i32 %s2, %m2
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !2
exit:
  %r = phi i32 [0, %entry], [%s2.n, %loop]
  ret i32 %r
}

; Disable + trip facts on one loop ID: the engine's static-trip proof
; reads itercount.range; the G009 veto reads the disable string first.
!0 = distinct !{!0, !1, !4}
!1 = !{!"llvm.loop.pipeline.disable", i1 true}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.itercount.range", i32 8}
!4 = !{!"llvm.loop.itercount.range", i32 8}
