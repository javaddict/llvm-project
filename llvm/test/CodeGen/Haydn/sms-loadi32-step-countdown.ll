; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=pipeliner < %s -o - \
; RUN:     | FileCheck %s --check-prefix=MIR
; REQUIRES: asserts

; Role: semantic -- LOADI32 countdown-step recognition feeds a correct SMS
; prologue-skip guard. This loop has no intra-bundle RAW on the compare flag,
; so it isolates the LOADI32-step fix (getInductionStep) from the D999
; no-forwarding spacing change (covered by sms-no-forwarding-raw-reject.ll).

; REGRESSION TEST (contract): SMS prologue-skip guard reads the IV init.
;
; Bug: HaydnInstrInfo::getInductionStep recognized a constant IV step only when
; materialized as ADDI32 $r0, imm -- either the direct ADDI32/ADDI32_W bump, or
; the register source of ADD32/SUB32. It did NOT recognize the step when LSR
; materialized it as LOADI32 imm (a single immediate operand, no R0 source).
; CoreMark matrix_sum's countdown step is exactly that shape:
;   %step = LOADI32 -1
;   %bump = ADD32 %iv, %step
; getInductionStep returned false, findInductionVar left Step at its default 0,
; and analyzeSimpleLoop took the `Step not < 0` branch -- setting the runtime
; TripCountReg to the compare's non-IV operand (the function-wide zero register,
; LOADI32 0) instead of the IV init (arg N). The prologue-skip guard became
; `SLT32 zero, TC+1` which is ALWAYS true, so the guard never disabled the
; kernel, the pipelined kernel was dead code, and only the scalar epilogue ran.
; matrix_sum thus returned ~1/9 of its expected accumulation.
;
; Fix: getInductionStep now recognizes `LOADI32 imm` as a step materialization
; (immediate at operand 1, no source register) alongside the existing
; `ADDI32 $r0, imm` form. For this loop Step resolves to -1, analyzeSimpleLoop
; takes the Step<0 branch, and TripCountReg becomes the IV's preheader init
; (arg N in $r2), so the guard is `SLT32 N, TC+1` -- false for N>=2 and the
; pipelined kernel is reachable.
;
; Contract checked below:
;   Pipeliner trace -- analyzeLoop succeeds (LOADI32 step recognized, not
;     "Unable to analyzeLoop") and a schedule is found ("Schedule Found? 1").
;     (The bespoke post-RA multi-stage engine and its handoff-switch corpus
;     were deleted by W68.1 — generic pre-RA MachinePipeliner owns SMS; the
;     former naive-handoff fixture is gone.) The schedule may still be
;     Target-rejected after Found? 1; that is orthogonal to LOADI32 step
;     recognition.
;   MIR -- after pipeliner, the countdown step materialization is still LOADI32
;     -1 (or an equivalent ADDI32 -1) and the IV init COPY of $r2 survives.
;     With the getInductionStep bug the loop was still analyzable only via the
;     wrong Step>=0 branch; the durable pin is "analyze succeeds + countdown
;     step present", not a multi-stage expanded prologue guard (guard pin was
;     multi-stage-only and is covered when handoff re-enables).
;
; What breaks if this regresses: "Unable to analyzeLoop" returns (LOADI32 step
; missed again) or the countdown step materialization disappears from the MIR.

; SWP: Schedule Found? 1
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; MIR-DAG: {{%[0-9]+}}:gpr32 = COPY $r2
; MIR-DAG: {{LOADI32|ADDI32|ADDI32_W}}{{.*}}-1

define i64 @loadi32_step_countdown(ptr nocapture readonly %p, i32 %n) #0 {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %loop, label %exit

loop:
  %i = phi i32 [ %n, %entry ], [ %i.dec, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  %pi = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %v64 = zext i32 %v to i64
  %acc.n = add nsw i64 %acc, %v64
  %i.dec = add nsw i32 %i, -1
  %cond = icmp sgt i32 %i.dec, 0
  br i1 %cond, label %loop, label %exit

exit:
  %r = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  ret i64 %r
}

attributes #0 = { "target-features"="-hwloop" }
