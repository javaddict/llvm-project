; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk

; G009 LOOP PRAGMA: per-loop state reset — no pragma leakage (one function).
;
; REGRESSION TEST CONTRACT: llvm.loop.pipeline.disable on the FIRST of two
; loops in ONE function must not leak to the SECOND. The generic pre-RA
; pipeliner resets pragma state per loop (MachinePipeliner::
; setPragmaPipelineOptions: "Reset the pragma for the next loop in
; iteration" — the Hexagon swp-pragma-disable-bug lesson, where a pragma on
; one loop suppressed a later loop in the same function). The post-RA
; engine holds the same law by construction: loopPipelinePragma is a
; per-call value read from each loop's own terminator MD — no member
; state, nothing to reset. This test locks the observable contract.
;
; Test design: two dual-store accumulator loops in one function. The
; FIRST carries pipeline.disable (plus itercount facts so it WOULD accept
; without the veto); the SECOND is pragma-free and, as the function's last
; loop, is the census-clean acceptor position (baseline: kind=accepted).
; If pragma state leaked forward, the second loop would come out
; not-candidate seat=pragma-disable. RMK pins: vetoed line with the pragma
; seat, then the clean accepted line (NO seat), then nothing more.

target triple = "haydn-unknown-elf"

; First loop: vetoed by its own pragma (pragma-disable works at any loop
; position — the veto fires at the engine candidate gate).
; RMK: Schedule found II=[[VII:[0-9]+]] NS=0 prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=not-candidate seat=pragma-disable
; RMK-SAME: loop=bb.{{[0-9]+}}.first
; Second loop, same function, no pragma: MUST still accept with NO seat
; (kind=accepted never carries a seat; leakage would flip it to
; not-candidate/pragma-disable).
; RMK: Schedule found II=[[AII:[0-9]+]] NS=[[ANS:[0-9]+]] prologue=
; RMK-SAME: {{[0-9]+}} parcels epilogue={{[0-9]+}} parcels kind=accepted
; RMK-SAME: loop=bb.{{[0-9]+}}.second
; Exactly two canonical lines; the accepted line carries no seat.
; RMK-NOT: Schedule found

; ASM: two_loops:
; ASM: jalr

define void @two_loops(ptr readonly %a, ptr readonly %b, ptr %d1, ptr %d2, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre1, label %exit
pre1:
  br label %first
first:
  %i1 = phi i32 [0, %pre1], [%i1.n, %first]
  %s1 = phi i32 [0, %pre1], [%s1.n, %first]
  %pa1 = getelementptr inbounds i32, ptr %a, i32 %i1
  %pb1 = getelementptr inbounds i32, ptr %b, i32 %i1
  %va1 = load i32, ptr %pa1, align 4
  %vb1 = load i32, ptr %pb1, align 4
  %m1 = mul i32 %va1, %vb1
  %s1.n = add i32 %s1, %m1
  store i32 %s1.n, ptr %d1, align 4
  %i1.n = add i32 %i1, 1
  %c1 = icmp ult i32 %i1.n, %n
  br i1 %c1, label %first, label %pre2, !llvm.loop !0
pre2:
  br label %second
second:
  %i2 = phi i32 [0, %pre2], [%i2.n, %second]
  %s2 = phi i32 [0, %pre2], [%s2.n, %second]
  %pa2 = getelementptr inbounds i32, ptr %b, i32 %i2
  %pb2 = getelementptr inbounds i32, ptr %a, i32 %i2
  %va2 = load i32, ptr %pa2, align 4
  %vb2 = load i32, ptr %pb2, align 4
  %m2 = mul i32 %va2, %vb2
  %s2.n = add i32 %s2, %m2
  store i32 %s2.n, ptr %d2, align 4
  %i2.n = add i32 %i2, 1
  %c2 = icmp ult i32 %i2.n, %n
  br i1 %c2, label %second, label %exit, !llvm.loop !2
exit:
  ret void
}

; Loop 0: disable veto + trip facts (would accept without the veto).
!0 = distinct !{!0, !1, !4}
!1 = !{!"llvm.loop.pipeline.disable", i1 true}
; Loop 2: trip facts only — the clean acceptor.
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.itercount.range", i32 8}
!4 = !{!"llvm.loop.itercount.range", i32 8}
