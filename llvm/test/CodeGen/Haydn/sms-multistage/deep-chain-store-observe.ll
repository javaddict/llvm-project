; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 OBSERVATION fixture (G002 follow-up; owner topics/scheduling).
; Class: deep mul-chain feeding the accumulator + per-iteration store.
;
; OPEN CLASSIFICATION (2026-08-23): on this body the canonical remark
; reports II=9 while the AsmPrinter AchievedII stamp reports 10 kernel
; parcels (verdict=schedule-limited) — the ONE known shape where the two
; counters diverge. Either (a) legitimate dest-window stall accounting
; (realized stream legitimately exceeds the searched II and the remark's
; parity certificate does not cover this stall class), or (b) a realized
; certificate gap. Until classified, this fixture PINS BOTH VALUES
; EXACTLY (the divergence IS the observation) — the README auto-update
; policy applies to the RMK II only under the scheduling owner's signoff.
;
; RMK: Schedule found II=9 NS=1 prologue=0 parcels epilogue=0 parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: k:
; ASM: set_hwloop_f2
; ASM: #<swps> II=9 cycles per pipeline stage (SMS schedule)
; ASM: #<swps> AchievedII=10 (kernel parcels)
; ASM: jalr

target triple = "haydn-unknown-elf"

define void @k(ptr readonly %a, ptr readonly %b, ptr %dst, i32 %n) {
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
  %pd = getelementptr inbounds i32, ptr %dst, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %t0 = add i32 %m, 1
  %t1 = mul i32 %t0, 3
  %t2 = add i32 %t1, %va
  %t3 = xor i32 %t2, %s
  %s.n = add i32 %s, %t3
  store i32 %s.n, ptr %pd, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
