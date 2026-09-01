; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk

; G009 LOOP PRAGMA: llvm.loop.pipeline.initiationinterval pins the II.
;
; REGRESSION TEST CONTRACT: initiationinterval N pins the post-RA engine's
; II search to EXACTLY N — an accepted schedule must report searched-II ==
; N in the canonical line (G002 certificate chain: pragma II == searched
; II == realized II). AIE discipline (AIEPostPipeliner TargetII): the
; solver is gated to the pragma II; Haydn additionally declines fail-closed
; when N is infeasible (see sms-pragma-ii-infeasible.ll) rather than
; searching elsewhere. The pre-RA generic pipeliner parses the same pragma
; (II_setByPragma -> MII/MAX_II in setMII/setMAX_II).
; Peer shape: Hexagon swp-pragma-initiation-interval.ii.
;
; Test design: dualacc kernel (natural search finds II=4, NS=1) pinned to
; II=4 via pragma — a pin the engine can honor. RMK pins the canonical
; line with the literal II=4 (the pin IS the contract here; capturing it
; would make the test vacuous) and NS captured (stage count is not pinned
; by the pragma). Control arm in-file: same kernel without the pragma
; also lands on II=4 (the natural search agrees — proving the engine did
; not merely echo the pragma but found a real schedule at N).

target triple = "haydn-unknown-elf"

; Pinned loop: accepted at EXACTLY the pragma II.
; RMK: Schedule found II=4 NS=[[PNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.loop
; Control loop: no pragma — natural search, also II=4 (engine liveness;
; the pin arm did not silently relax).
; RMK: Schedule found II=4 NS=[[CNS:[0-9]+]] prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.loop
; RMK-NOT: Schedule found

; ASM: pinned:
; ASM: jalr
; ASM: control:
; ASM: jalr

define i32 @pinned(ptr readonly %a, ptr readonly %b, i32 %n) {
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

!0 = distinct !{!0, !1, !4}
!1 = !{!"llvm.loop.pipeline.initiationinterval", i32 4}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.itercount.range", i32 8}
!4 = !{!"llvm.loop.itercount.range", i32 8}
