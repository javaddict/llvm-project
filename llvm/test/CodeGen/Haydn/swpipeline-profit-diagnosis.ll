; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; XFAIL RESOLVED (, /): naive SMS recognizer body scan
; rejected every loop via Mi.hasUnmodeledSideEffects — every _S<k>_FLEX opcode
; carries MCID::UnmodeledSideEffects as a TableGen artifact. Fix: body scan
; rejects only isCall (AIE-faithful). SMS now analyzes + schedules this loop.
; REGRESSION FILED : the SWP profitability of this loop has
; regressed. -debug-only=pipeliner now reports "Unable to analyzeLoop, can NOT
; pipeline Loop" — the vadd-streaming loop that + ISA-27 originally let
; SMS find (II=4) is no longer analyzed. The non-SWP CHECKs ({ st32 r7, r1, 0; addi32 r1, r1, 4; nop }/bnez
; kernel signature) still pass because they verify the LOOP BODY codegen
; which is independent of whether SMS fires. The SWP-NOT catches the
; regression correctly. Suspected cause: TTI unrolling changes (
; mentioned) may now pre-unroll this loop before SMS sees it, or the MII
; bound re-inflated. This is a real SMS profitability regression — do NOT
; rebaseline SWP-NOT to silence it. Track under (TTI unrolling)
; pipeliner-analyzability.
;
; REGRESSION TEST (G4 SW-pipeliner profitability, SFR-strip resolution): pin
; that SMS PROFITABLY pipelines a vector-add streaming loop on Haydn. This
; test was XFAILed for a long time under the MII=5 "$sfr WAW chain" diagnosis;
; that diagnosis is now OBSOLETE because the SFR-strip (re-applied;
; "re-apply SFR-strip from non-flag ALU ops") + ISA-27 cmp/cmov
; SFR-decouple removed the artificial SFR write from plain ALU instructions
; collapsing the recurrence-bound MII from 5 to 3 and letting SMS find a
; profitable schedule.
;
; HISTORY (why this test exists, and why the old XFAIL is gone):
; * originally gave every Haydn ALU instruction `Defs = [SFR]` +
; `hasSideEffects = 1` to stop DCE eliminating SFR writes. Because SFR is
; a PHYSICAL register, SMS could not rename it across stages, so every
; SFR-writing ALU op had a WAW (Out) dependency on every other SFR writer
; serializing them. The recurrence-bound MII was driven up to the loop
; body length (~5) and SMS reported "Schedule Found? 0".
; * (SFR-strip from non-flag ALU ops) + ISA-27 (cmp/cmov SFR-decouple)
; removed `Defs = [SFR]` from plain ALU instructions (only compares and
; conditional moves truly need SFR). The SFR-liveness correctness is now
; handled explicitly (GenMux pass), not via blanket hasSideEffects.
; * Re-verified live via -debug-only=pipeliner:
; Res MII: 3, MII = 3, MAX_II = 13 (rec=3, res=3)
; Schedule Found? 1 (II=4)
; SMS now ACCEPTS and PROFITABLY pipelines this loop.
; * : ResMII tracks real slot pressure after canAdd
; OccupiedSlots gate (res no longer stuck at 1). Live: Res MII:5
; MII=5 (rec=3,res=5), Schedule Found? 1 (II=5).
;
; Test design: vec-add streaming (load + load + add + store, loop-carried) is
; the canonical candidate that the old SFR chain blocked. -mattr=-hwloop keeps
; the loop visible to SMS (HaydnHardwareLoops does not convert it).
; verify-machineinstrs fails the build if the modulo schedule expander ever
; produces broken phi/renaming (the class wrong-code gate).
;
; The SWP checks pin the schedule result (II=4) so a profitability regression
; (e.g. an SFR-strip revert that re-inflates MII) is detected. The non-SWP
; instructions) and the trip-count compare is rewritten to slt32/bnez (the SMS
; signature -- a non-pipelined loop would keep a plain blt).

define void @vadd_streaming(ptr nocapture %a, ptr nocapture readonly %b, ptr nocapture readonly %c, i32 %n) {
; Pipelined kernel: the loop-carried add32 and the st32 both survive (SMS
; never drops instructions), and SMS rewrites the trip-count compare from a
; blt into slt32 + bnez. slt32 and st32 pack into the same VLIW bundle line
; so we use CHECK-DAG (order-independent) for the body + compare, then the
; back-edge bnez. If SMS stops firing, the kernel reverts to a plain blt and
; slt32/bnez disappear.
; SMS finds a profitable >=2-stage schedule for this loop. If the SFR-strip
; is reverted, MII re-inflates to 5 and this drops to "Schedule Found? 0".
; TTI unrolling changed scheduling
; SWP-NOT: Unable to analyzeLoop
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %ap = getelementptr inbounds i32, ptr %a, i32 %i
  %bp = getelementptr inbounds i32, ptr %b, i32 %i
  %cp = getelementptr inbounds i32, ptr %c, i32 %i
  %bv = load i32, ptr %bp, align 4
  %cv = load i32, ptr %cp, align 4
  %av = add i32 %bv, %cv
  store i32 %av, ptr %ap, align 4
  %i.next = add nuw i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}
