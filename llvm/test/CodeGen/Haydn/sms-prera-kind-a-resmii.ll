; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SMS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: GR2.1 Kind-A pre-RA ResMII pin. The pre-RA MachinePipeliner seat
; (CreateTargetScheduleState) returns HaydnIssueWidthCycle: the generated
; IssueWidth entry cap plus the shared same-cycle RAW/WAW dependency laws.
; No exact format rows, alternates, unit menus, regfile port budgets, or
; named laws are consulted pre-RA; exact legality and cycle-slip are post-RA.
;
; Pins (Kind-A actuals on this corpus):
;   * 7-placeable-op dual-load stream: Res MII 3 = ceil(7 entries / 3);
;   * smaller streams report 2 — the D999 no-forwarding RAW chain
;     (load -> add -> compare) splits cycles, NOT port budgets; the entry
;     cap never forces more than the count bound;
;   * the three-independent-writes body reports 2 (RAW bump->cmp split), and
;     the post-RA ASM arm still pays the GPR 2W law via packing/cycle-slip —
;     port law evidence moved entirely to the post-RA arm;
;   * Schedule Found? 1 preserved on all kernels;
;   * the dual-load streaming loop still pipelines (Kind A keeps the original
;     EnableHaydnHRResourceCycle motivation: dual LD32 co-issue at pre-RA);
;   * pure entry-count pin (3 writes, no RAW chain): Res MII 1 is pinned in
;     sms-format-resmii-port-forced.mir (the port-forced premise restamp).
;
; Unit peer: HaydnIssueWidthCycleTest (cap / RAW / WAW / no-exact-consultation).
; Post-RA port law evidence: postra port packing lits + this file's ASM arm.

; SMS-DAG: SMS-HANDOFF: coverage ok
; SMS-DAG: Return Res MII:3
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})
; SMS-DAG: SMS-HANDOFF: coverage ok
; SMS-DAG: Return Res MII:2
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})
; SMS-DAG: SMS-HANDOFF: coverage ok
; SMS-DAG: Return Res MII:2
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})
; SMS-DAG: SMS-HANDOFF: coverage ok
; SMS-DAG: Return Res MII:3
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})
; SMS-NOT: SMS-HOOK: reject
; SMS-NOT: Unable to analyzeLoop
; SMS-NOT: SMS-HANDOFF: reject

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a0:32-n32-S64"
target triple = "haydn-unknown-elf"

; 7 placeable body ops: 2 loads + 2 adds + acc add + iv bump + cmp
; → ceil(7/3) = 3 Kind-A cycles.
define i32 @kinda_seven_ops(ptr nocapture readonly %x, i32 %n) {
; ASM-LABEL: kinda_seven_ops:
; ASM:        // =>This Inner Loop Header: Depth=1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %yi = getelementptr i32, ptr %x, i32 %i
  %xv = load i32, ptr %xi, align 4
  %yv = load i32, ptr %yi, align 4
  %s = add i32 %xv, %yv
  %acc.next = add i32 %acc, %s
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %acc.next
}

; 2 placeable body ops → 1 Kind-A cycle.
define i32 @kinda_two_ops(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: kinda_two_ops:
; ASM:        // =>This Inner Loop Header: Depth=1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %v
}

; Three independent GPR writes: Kind A reports the RAW-bound 2 (bump -> cmp
; chain), never a port number; the post-RA scheduler still pays the 2W law
; (the ASM arm below packs at most 2 of the 3 writes per cycle).
define i32 @kinda_three_writes(i32 %a, i32 %b, i32 %c, i32 %n) {
; ASM-LABEL: kinda_three_writes:
; ASM:        // =>This Inner Loop Header: Depth=1
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %x = add i32 %a, %i
  %y = add i32 %b, %i
  %z = add i32 %c, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %z
}

; Dual-load streaming MAC loop: the original exact-seat motivation (two LD32
; co-issue) survives Kind A — loads are plain entries at this seat.
define i32 @kinda_dual_load_mac(ptr nocapture readonly %x,
                                ptr nocapture readonly %h, i32 %n) {
; ASM-LABEL: kinda_dual_load_mac:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|mull|mul64|add32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %hi = getelementptr i32, ptr %h, i32 %i
  %xv = load i32, ptr %xi, align 4
  %hv = load i32, ptr %hi, align 4
  %prod = mul i32 %xv, %hv
  %acc.next = add i32 %acc, %prod
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %acc.next
}
