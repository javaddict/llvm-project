; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM

; Role: semantic — XFAIL RESOLVED (, /): naive SMS recognizer body scan rejected every loop via Mi.hasUnmodeledSideEffects — every _S<k>_FLEX opcode.

; XFAIL RESOLVED (, /): naive SMS recognizer body scan
; rejected every loop via Mi.hasUnmodeledSideEffects — every _S<k>_FLEX opcode
; carries MCID::UnmodeledSideEffects as a TableGen artifact (HaydnFormatInst
; lacks hasSideEffects=0). Fix: body scan rejects only isCall (AIE-faithful).
; SMS now analyzes + schedules these loops.
; REGRESSION FILED : SMS analyzability regression. The
; trip-count-derivation fix that previously let SMS pipeline both decrementing
; loops (and avoid the SlotIndexes crash) no longer fires — both loops now
; report "Unable to analyzeLoop, can NOT pipeline Loop". The ASM check still
; passes (proves no crash and.text is non-empty), so the crash class is
; still defended, but the SWP profitability regressed. Suspected cause: TTI
; unrolling changes or a pipeliner-loop-limit gate. Track under
; pipeliner-analyzability. Do NOT rebaseline SWP CHECKs to silence the
; regression.
;
; REGRESSION TEST: SMS trip-count derivation for decrementing IVs (/).
;
; Bug: analyzeSimpleLoop took the trip-count register from the comparison's
; NON-induction operand. For LSR'd decrementing loops -- the shape every real
; DSP kernel lowers to, `iv += -1; SEQ32 iv, %zero; BNEZ` -- that operand is a
; function-wide materialized zero (`ADDI32 $r0, 0`, reused as the zero input
; of EVERY compare, PHI-init, and call arg in the function), NOT the trip
; count. Two consequences:
; (1) CRASH: adjustTripCount did MRI.replaceRegWith(%zero, NewTC), rewriting
; that shared zero register everywhere. When a SECOND loop in the same
; function was then pipelined, registerPressureFilter queried
; LiveIntervals for a vreg created by the first loop's expansion (e.g.
; %537 = SLT32 %581, %536) that was never indexed into SlotIndexes ->
; SlotIndexes.h "Instruction not found in maps." assertion
; (core_matrix.c / matrix_test). Proven: those vregs do not exist in the
; pre-pipeliner MIR (highest vreg %484); -pipeliner-loop-limit=1 avoids
; the crash.
; (2) WRONG CODE: createTripCountGreaterCondition emitted `SLT32 %zero, TC+1`
; = `0 < TC+1` = always-true, so the runtime guard never disabled the
; kernel for small trip counts (same disease class as).
; Fix: derive the trip count from the IV PHI's PREHEADER incoming (the init
; value) for decrementing IVs -- the IV counts N, N-1,..., 0, so its initial
; value IS the trip count. This mirrors AIE's DownCountLoop::accept.
;
; Test design: TWO decrementing countable loops in ONE function (the
; matrix_test bb.5/bb.9 shape, replicated) so SMS expands loop 1 and then
; reaches loop 2 -- the exact configuration that exposed the
; stale-LiveIntervals crash via the corrupted shared-zero trip-count register.
; If the trip-count fix regresses, llc crashes with the SlotIndexes assertion
; while scheduling the second loop. The ASM check confirms.text is non-empty
; (the MAC write-back false-PASS trap: a kernel that emits empty.text because
; the result was DCE'd is a FAILURE, not a pass).

; SWP: Schedule Found? 1
; SWP: Schedule Found? 1
; SWP-NOT: Instruction not found in maps
; SWP-NOT: Assertion
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

define void @two_decrementing_loops(i32 noundef %N, ptr nocapture %A, ptr nocapture %B) {
; ASM-LABEL: two_decrementing_loops:
; ASM:        jalr
entry:
  %cmpA = icmp eq i32 %N, 0
  br i1 %cmpA, label %loopB, label %loopA

loopA:
  %iA = phi i32 [ %decA, %loopA ], [ %N, %entry ]
  %pa = getelementptr i32, ptr %A, i32 %iA
  store i32 %iA, ptr %pa, align 4
  %decA = add i32 %iA, -1
  %cmpA2 = icmp eq i32 %decA, 0
  br i1 %cmpA2, label %loopB, label %loopA

loopB:
  %iB = phi i32 [ %decB, %loopB ], [ %N, %loopA ], [ %N, %entry ]
  %pb = getelementptr i32, ptr %B, i32 %iB
  store i32 %iB, ptr %pb, align 4
  %decB = add i32 %iB, -1
  %cmpB = icmp eq i32 %decB, 0
  br i1 %cmpB, label %exit, label %loopB

exit:
  ret void
}
