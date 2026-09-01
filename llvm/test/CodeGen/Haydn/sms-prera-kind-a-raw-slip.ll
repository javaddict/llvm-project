; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SMS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: GR2.1 — re-pin the D999 miscompile class at the narrowed Kind-A seat.
; HaydnIssueWidthCycle enforces the SHARED same-cycle dependency laws with the
; exact ordering of the old exact seat (check-before-accept, defs appended
; after commit):
;   * no-forwarding intra-cycle RAW: a consumer never issues in the same
;     modulo cycle as a live def it reads (CoreMark matrix_sum OOB class,
;     pinned end-to-end by sms-no-forwarding-raw-reject.ll — control, must
;     stay green unmodified);
;   * same-phase no-dual-write WAW: two writers of one register never share
;     a cycle.
; These are same-cycle dependency laws, not row/unit/port matching, so they
; remain legal pre-RA feasibility (the same family the IsPreRA MISCHED HR
; enforces).
;
; Pins: the producer+consumer pair never shares an ASM bundle line (the
; consumer slips to a later cycle); Schedule Found? 1; the WAW companion
; shape schedules with its writers split across cycles.

; SMS-DAG: SMS-HANDOFF: coverage ok
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})
; SMS-NOT: SMS-HOOK: reject
; SMS-NOT: Unable to analyzeLoop

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a0:32-n32-S64"
target triple = "haydn-unknown-elf"

; RAW shape: load feeds add in the same iteration. No intra-cycle forwarding:
; the add cannot ride the load's cycle; it slips one cycle.
define i32 @raw_pair(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: raw_pair:
; ASM:        // =>This Inner Loop Header: Depth=1
; Producer and consumer are never on the SAME bundle line.
; ASM-NOT:    { {{[^}]*}}ld32{{[^}]*}} {{[^}]*}}add32{{[^}]*}} }
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %acc.next
}

; WAW companion: two writers of the same register (%w via phi update chain)
; can never share a cycle even under the pure entry-count model.
define i32 @waw_companion(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: waw_companion:
; ASM:        // =>This Inner Loop Header: Depth=1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %w = phi i32 [ 0, %entry ], [ %w.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %t = add i32 %w, %v
  %w.next = add i32 %t, 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %w.next
}
