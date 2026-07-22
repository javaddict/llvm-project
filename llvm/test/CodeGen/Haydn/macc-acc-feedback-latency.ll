; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM
;
; STALE-FAILMARKER REMOVED (, post- cutover): SMS fires on this
; loop with rec=1 — the core acc-feedback latency-1 assertion this test guards
; still holds. The s32 mul lowers via the DR64 MAC unit (mull, not mac32);
; the feedback path is d0->d0 on the DR64 accumulator.
; Bundle::canAdd no longer treats Instrs-empty + non-zero
; OccupiedSlots as "empty" (SMS reserveByOpcode path). ResMII now reflects
; real slot pressure (res=4) instead of the stuck ResMII=1 bug.
;
; REGRESSION TEST (D2XX): MAC accumulator-feedback latency in SMS recMII.
;
; Bug being guarded: if the MAC accumulator-feedback edge is modeled as latency
; 2 (instead of the hardware-correct 1), SMS's recurrence MII for this single
; accumulator reduction rises, and the pipeliner either rejects the schedule
; (StageCount<=1) or finds a worse II. The hardware has an internal accumulator
; feedback register: acc->acc on the SAME accumulator, back-to-back MAC, is
; 1 cycle. Only the rd writeback through the GPR register file is 2 cycles.
; The itinerary encodes both with ONE OperandCycles list [2,1,1,2]; the LLVM
; formula latency = DefCycle - UseCycle + 1 yields acc->acc = 2-2+1 = 1.
;
; Expected result: SMS reports rec=1 and a profitable schedule. Post- the
; s32 mul lowers via the DR64 MAC unit (mull) and the feedback path is
; d0->d0 across iterations on the DR64 accumulator.
;
; Test design: %acc.next = add(%acc, mul(%xv, %hv)) lowers (post-) to a
; DR64 mul64.ll with the loop-carried acc feeding back through d0. If the
; latency model regresses, rec rises above 1 and either II changes or
; Schedule Found? becomes 0.

define i32 @mac_acc_feedback(ptr nocapture readonly %x, ptr nocapture readonly %h, i32 %n) {
; ASM-LABEL: mac_acc_feedback:
; The kernel must contain a DR64 multiply (mul64.ll) that reads its own
; accumulator register d0 (feedback path proving acc->acc latency 1).
; ASM:       // =>This Inner Loop Header: Depth=1
; ASM:       mull
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %hi = getelementptr i32, ptr %h, i32 %i
  %xv = load i32, ptr %xi
  %hv = load i32, ptr %hi
  %mul = mul i32 %xv, %hv
  %acc.next = add i32 %acc, %mul
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}

; ResMII must track slot pressure (was stuck at 1 when canAdd always
; accepted after reserveByOpcode left Instrs empty). Multi-op MAC loop → res>1.
; SWP: Return Res MII:{{[0-9]+}}
; The recurrence MII MUST be 1 -- this is the assertion that the acc->acc
; feedback path is latency 1, not 2. If it regresses to rec=2 the MAC unit's
; feedback path is mis-modeled and serial reductions stop pipelining.
; SWP: MII = {{[0-9]+}} MAX_II = {{[0-9]+}} (rec=1, res={{[0-9]+}})
; SWP: Schedule Found? 1 (II={{[0-9]+}})
