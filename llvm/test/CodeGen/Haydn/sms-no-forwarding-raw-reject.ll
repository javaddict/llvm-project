; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: semantic — SMS no-forwarding intra-bundle RAW (D999). Producer (SEQ32
; from `icmp eq`) and its consumer (XORI32 from `xor .. 1`) must NOT co-issue
; in the same Format E bundle: Haydn spec §Constraints — all instructions in a
; bundle read their sources simultaneously, so a same-cycle reader of a live
; def observes the OLD value (no intra-bundle forwarding).

; REGRESSION TEST (contract): SMS placement rejects an intra-bundle RAW.
;
; Bug (CoreMark matrix_sum inner loop): Haydn SMS placement called the
; MCInstrDesc overload of ResourceCycle::canReserveResources (descriptor-only,
; no operands), so it could NOT see the no-forwarding intra-bundle RAW law that
; post-RA HaydnHazardRecognizer::hasSameBundleRAW enforces. SMS therefore
; scheduled a producer (SEQ32 writing the compare flag) and its consumer
; (XORI32 inverting that flag) into the SAME modulo phase = the SAME runtime
; bundle. At issue the consumer read the OLD flag value, so the loop-control
; predicate was wrong, the inner loop executed ONE extra iteration (10 instead
; of 9), and matrix_sum read one element past the 32-element row -> OOB read ->
; wrong accumulation -> CoreMark CRC mismatch (list=0xe714 matrix=0x?? vs the
; host's matrix=0x1fd7). Runtime-confirmed: the 10th inner iter is an OOB read.
;
; Fix (D999): SMS placement now calls the operand-aware MachineInstr overload
; (llvm/lib/CodeGen/MachinePipeliner.cpp canReserveResources/reserveResources
; pass *SU.getInstr() instead of &SU.getInstr()->getDesc()). The MI overload in
; HaydnResourceCycle runs the shared no-forwarding predicate
; (HaydnIntraCycleRAW.h — the ONE mechanism also used by post-RA HR, hard #7)
; and rejects the consumer when its source is a live def earlier in this cycle;
; reserveResources appends the consumer's live defs. The consumer then slips to
; a LATER cycle, the hazardous bundle { seq32...; xori32 r, r, 1 } is NEVER
; formed, the inner loop runs the correct count (9), no OOB, matrix_sum=810,
; and CoreMark CRCs match the host (list=0xe714 matrix=0x1fd7 state=0x8e3a).
; SMS STILL pipelines (correctness AND performance) — there is no scalar
; fallback; the consumer is simply spaced to a later cycle.
;
; Contract:
;   Pipeliner trace -- SMS finds a schedule (no scalar fallback / no reject):
;   "Schedule Found? 1".
;   Assembly -- no single bundle in the function contains both `seq32` and its
;        same-cycle flag consumer. In this IR the SEQ32 flag is consumed by the
;        AND32 mask that canonicalizes it to 0/1 (the zext of the icmp); the
;        XOR32 inverts the masked value afterwards. The D999 no-forwarding law
;        forbids SEQ32 and that AND32 reader from sharing a bundle. A bundle
;        prints on one line as `{ op; op; op }`; if the bug regresses, SMS packs
;        the producer+reader into one modulo phase and that line appears.
;
; What breaks if this regresses: the ASM-NOT bundle re-appears (SMS re-packs the
; RAW pair same-cycle) and at runtime matrix_sum's inner loop over-reads ->
; CoreMark matrix CRC diverges from the host golden (0x1fd7).

; SWP: Schedule Found? 1
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: no_forwarding_raw:
; No bundle may contain both seq32 (flag producer) and and32 (its consumer).
; ASM-NOT: {{[{].*seq32.*and32}}
; ASM-NOT: {{[{].*and32.*seq32}}
; ASM: jalr

define i32 @no_forwarding_raw(ptr nocapture readonly %p, i32 %n) {
entry:
  %c0 = icmp sgt i32 %n, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %pi = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %eq = icmp eq i32 %v, 0           ; lowers to SEQ32 -- produces the flag
  %eq.i32 = zext i1 %eq to i32      ; materialize flag as i32 (AND32 masks the SEQ32 result to 0/1)
  %inv = xor i32 %eq.i32, 1         ; lowers to XORI32 -- inverts the masked flag
  %s.next = add i32 %s, %inv
  %i.next = add nuw nsw i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  ret i32 %r
}
