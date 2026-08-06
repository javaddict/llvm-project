; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -stop-after=pipeliner < %s -o - \
; RUN:     | FileCheck %s --check-prefix=MIR
; REQUIRES: asserts

; Role: semantic -- latch SEQ + XORI invert stay stage-0 control (20000605-1).
;
; REGRESSION TEST (contract): SMS must not stage the XORI invert of the latch
; compare while leaving SEQ/BNEZ in stage 0.
;
; Bug: analyzeSimpleLoop peels
;   cmp = SEQ32 iv, limit
;   inv = XORI32 cmp, 1
;   BNEZ inv, loop
; so CmpMI is the SEQ. shouldIgnoreForPipelining ignored only CmpMI/EndLoop,
; leaving XORI schedulable. MachinePipeliner then placed XORI in stage 1
; while SEQ stayed stage 0. The expanded kernel branched on a PHI of the
; previous SEQ, delaying exit by one iteration. For the constant-folded
; countdown (gcc-torture 20000605-1: for y in 0..255 -> IV 256..0) that
; overshoot left the post-loop SEQ live-out false and aborted.
;
; Fix: track InvertMI (the XORI) and ignore it with CmpMI/EndLoop so the
; whole exit-control chain is unpipelineable stage-0. computeUnpipelineableNodes
; also pulls the IV bump into stage 0. Product multi-stage-naive reject
; (handoff off) further refuse bare multi-stage expansion.
;
; Contract checked below:
;   SWP -- "Do not pipeline" covers the XORI (SU that defines the invert).
;   SWP -- no accepted multi-stage rewrite of this latch (schedule not kept).
;   MIR -- single latch BB keeps SEQ + XORI + BNEZ together (no prolog PHI
;     of the SEQ flag into a multi-stage kernel).

; XORI is unpipelineable (InvertMI). Stage-forced with SEQ/IV.
; SWP: Do not pipeline SU({{[0-9]+}})
; SWP: XORI32
; Accepted multi-stage expansion must not stick for product handoff-off.
; SWP: No schedule found, return

; MIR-LABEL: name: countdown_seq_xori
; MIR: bb.{{[0-9]+}}.loop:
; MIR: SEQ32
; MIR: XORI32
; MIR: BNEZ_W
; No second loop/kernel BB from a multi-stage expand (single latch).
; MIR-NOT: bb.{{[0-9]+}}.loop:

define i32 @countdown_seq_xori() {
entry:
  br label %loop

loop:
  %iv = phi i32 [ 256, %entry ], [ %iv.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %acc.next = add nuw nsw i32 %acc, 1
  %iv.next = add nsw i32 %iv, -1
  %cond = icmp eq i32 %iv.next, 0
  br i1 %cond, label %exit, label %loop

exit:
  ret i32 %acc.next
}
