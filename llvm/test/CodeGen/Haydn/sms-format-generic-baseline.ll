; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=BASE,PROD
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-matching-frontier=false \
; RUN:   -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=BASE,GEN
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-finer-rp-tracking=false \
; RUN:   -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefixes=BASE,RP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-PROD
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-matching-frontier=false -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-GEN
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-finer-rp-tracking=false -stats -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS-RP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stop-after=pipeliner -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=HANDOFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-matching-frontier=false \
; RUN:   -stop-after=pipeliner -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=HANDOFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stop-after=postmisched -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -haydn-premisched-matching-frontier=false \
; RUN:   -stop-after=postmisched -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: MIR — VF3-G2-GENERIC-BASELINE SMS slice (§8.3 / §8.4 #11 Generic-pass
; freeze). Owns HaydnResourceCycle + analyzeLoopForPipelining SMS gates + this
; dual-run corpus. Does NOT own pre-RA list HR / PreRASchedStrategy /
; matching-frontier implementation (sibling: prera-format-generic-baseline.ll).

; Full-only pre-RA generic-pass cycle/pressure dual-run baseline (format-SMS).
; Product ranking (matching-frontier ON + finer-rp ON, defaults) must match
; the frozen generic residual arms on ResMII/II/soft-exit/exact-pack KPIs.
;
; Dual-run arms (existing product flags only; never null pre-RA HR factory;
; isavail-delay stays default OFF; HANDOFF stays coverage fail-close only):
;   * PROD — product defaults
;   * GEN  — -haydn-premisched-matching-frontier=false (stock NodeOrder after
;            pressure/critical; cycle-ranking residual)
;   * RP   — -haydn-premisched-finer-rp-tracking=false (GenericScheduler
;            pressure-policy residual; AIE reduce_pressure peer surface)
;
; GR2.1 Kind-A restamp: the pre-RA seat reasons on the generated IssueWidth
; entry cap + shared same-cycle RAW/WAW only (the exact RESMII/FORMAT/QOR/IPC
; oracles and the exact-pack HANDOFF gate are deleted). ResMII is the generic
; calculateResMIIDFA count over the Kind-A cycle; the deleted exact floors
; (port 4/3, format 3/2) no longer inflate it:
;   * dual-load MAC stream: Res MII/II=3 (was 4), rec=1
;   * simple acc stream: Res MII/II=2 (was 3), rec=1
;   * SMS-HANDOFF is now the generated-coverage ok line; no reject
;   * HOOK rejects silent; Schedule Found? 1 both kernels
;   * -stop-after=pipeliner: no durable BUNDLE (coverage-only HANDOFF)
;   * -stop-after=postmisched: multi-MI BUNDLE 0 still legal both ranking modes
;   * -stats: multi-MI exact finalize parity; spill/reload/split/hard-root silent
;
; Soft-exit product-only peer: sms-format-qor-exit.ll
; Unit peer: HaydnBundleTest.SMSSoftExitQoRFloorsAndExactPack
; Pre-RA sibling dual-run: prera-format-generic-baseline.ll
; Dual-run stats pattern peer: regalloc-compact-hints.ll
; VF3-G3 ILP/critical dual-run residual attribution peer:
;   sms-format-ilp-crit-dual-run.ll (ILP multi-load / dual-acc + critical chain)

; --- Shared fail-closed silence (all ranking arms) ---
; BASE-NOT: SMS-HOOK: reject
; BASE-NOT: unsupported SMS-HOOK resource class
; BASE-NOT: Unable to analyzeLoop
; BASE-NOT: SMS-HANDOFF: reject

; --- PROD Kind-A ResMII/II (product ranking) ---
; PROD-DAG: SMS-HANDOFF: coverage ok
; PROD-DAG: Return Res MII:3
; PROD-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; PROD-DAG: Schedule Found? 1 (II=3)
; PROD-DAG: SMS-HANDOFF: coverage ok
; PROD-DAG: Return Res MII:2
; PROD-DAG: MII = 2 MAX_II = 12 (rec=1, res=2)
; PROD-DAG: Schedule Found? 1 (II=2)

; --- GEN Kind-A ResMII/II (matching-frontier OFF residual) ---
; GEN-DAG: SMS-HANDOFF: coverage ok
; GEN-DAG: Return Res MII:3
; GEN-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; GEN-DAG: Schedule Found? 1 (II=3)
; GEN-DAG: SMS-HANDOFF: coverage ok
; GEN-DAG: Return Res MII:2
; GEN-DAG: MII = 2 MAX_II = 12 (rec=1, res=2)
; GEN-DAG: Schedule Found? 1 (II=2)

; --- RP Kind-A ResMII/II (finer-rp OFF residual) ---
; RP-DAG: SMS-HANDOFF: coverage ok
; RP-DAG: Return Res MII:3
; RP-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; RP-DAG: Schedule Found? 1 (II=3)
; RP-DAG: SMS-HANDOFF: coverage ok
; RP-DAG: Return Res MII:2
; RP-DAG: MII = 2 MAX_II = 12 (rec=1, res=2)
; RP-DAG: Schedule Found? 1 (II=2)

; --- Dual-run -stats attribution (PROD vs GEN vs RP) ---
; FileCheck order follows -stats emission (post-RA first among these).
; WP5 product multi-stage: durable SMS groups enter post-RA as multi-member
; hard roots and exact-commit; free multi-MI packs still finalize as BUNDLE.
; Field-order RAW may fail exact no-split and sequentialize — correct product
; behavior (postra-field-order-raw-narrow-store.mir). Zero spill/reload on this
; body remains the QoR pin.
; STATS-PROD-NOT: SMS kernel same-cycle groups materialized
; STATS-PROD-DAG: multi-MI cycles finalized as BUNDLE
; STATS-PROD-NOT: Number of spills inserted
; STATS-PROD-NOT: Number of reloads inserted
;
; STATS-GEN-NOT: SMS kernel same-cycle groups materialized
; STATS-GEN-DAG: multi-MI cycles finalized as BUNDLE
; STATS-GEN-NOT: Number of spills inserted
; STATS-GEN-NOT: Number of reloads inserted
;
; STATS-RP-NOT: SMS kernel same-cycle groups materialized
; STATS-RP-DAG: multi-MI cycles finalized as BUNDLE
; STATS-RP-NOT: Number of spills inserted
; STATS-RP-NOT: Number of reloads inserted

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

; Dual-load MAC-shaped reduction — frozen Res/Rec/II under all ranking arms.
; Soft-exit II floor = max(format=3, ports=4) = 4; RecMII stays DDG (rec=1).
define i32 @baseline_sms_mac_acc_feedback(ptr nocapture readonly %x,
                                          ptr nocapture readonly %h, i32 %n) {
; HANDOFF-LABEL: name: baseline_sms_mac_acc_feedback
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|MULL|ADD32|ADDI32)}}
; HANDOFF-NOT: {{(LD32|S_LW|MULL|ADD32)}}_S
;
; POST-LABEL: name: baseline_sms_mac_acc_feedback
; POST: BUNDLE {{[01]}}
;
; ASM-LABEL: baseline_sms_mac_acc_feedback:
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

; Simple accumulation — frozen soft-exit II floor 3 / ResMII 3 under dual-run.
define i32 @baseline_sms_acc_stream(ptr nocapture readonly %p, i32 %n) {
; HANDOFF-LABEL: name: baseline_sms_acc_stream
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|ADD32|ADDI32)}}
; HANDOFF-NOT: {{(LD32|S_LW|ADD32)}}_S
;
; POST-LABEL: name: baseline_sms_acc_stream
; POST: S_LW_POST_IMM
;
; ASM-LABEL: baseline_sms_acc_stream:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|add32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi, align 4
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}
