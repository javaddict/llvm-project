; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.var.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=ACC < %t.var.rmk
; RUN: FileCheck %s --check-prefix=VAR < %t.var.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-analysis-only \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms \
; RUN:     -haydn-loop-min-tripcount=64 < %s 2>%t.floor.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=FLOOR < %t.floor.rmk
;
; REGRESSION TEST: F39 — reg-trip trip-count guard (QUALIFY blocker).
;
; Bug: hasSufficientTripCount's register-trip arm accepted ANY non-R0
; physreg trip source (and the soft-count arm any physical trip reg), while
; materialize() inserted the PrologMBB unconditionally. A runtime trip
; (function argument) smaller than NStages made the prologue peel stage
; operations of iterations that never execute (OOB loads/stores) while the
; remat/soft-adjust drove the count negative — wrong code with no runtime
; guard. AIE peer refuses: !hasSufficientMinTripCount && !peelSideEffectFree.
;
; Fix: provenMinTripCount — the trip source's preheader materialization
; chain (LOADI32 imm / ADDI/SUBI-on-R0 chains) is the only sound post-RA
; static oracle; else the llvm.loop.itercount.range MD; else the shared
; -haydn-loop-min-tripcount floor. Unknown trip fails closed at PF-TRIP.
;
; Test design (SF1 wiring note 2026-08-16): a reg-trip ZOL loop whose body
; the format oracle still admits — analyze ACCEPTS (ACC) so the preflight
; PF-TRIP seat is genuinely reached and exercised. VAR (no proof source:
; runtime %n, no MD, no floor) must reject at PF-TRIP. FLOOR
; (-haydn-loop-min-tripcount=64) must pass PF-TRIP (the loop may then
; reject at a later, unrelated seat — what matters is PF-TRIP is absent).
; If the F39 guard is dropped, VAR shows no PF-TRIP reject and the
; unguarded prologue path becomes reachable again.

; ASM-LABEL: runtime_trip_sum:
; ASM: jalr
; ACC: {{accepted II=|exhausted:|preflight reject: PF-TRIP}}
; VAR: {{preflight reject: PF-TRIP|exhausted:}}
; FLOOR: {{accepted II=|exhausted:|preflight reject:}}
; FLOOR-NOT: preflight reject: PF-TRIP
; FLOOR-NOT: sequential

define i32 @runtime_trip_sum(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [ 0, %pre ], [ %i.n, %loop ]
  %s = phi i32 [ 0, %pre ], [ %s.n, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %t0 = add i32 %v, 1
  %t1 = mul i32 %t0, 3
  %t2 = add i32 %t1, %v
  %t3 = xor i32 %t2, %s
  %s.n = add i32 %s, %t3
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s.n, %loop ]
  ret i32 %r
}
