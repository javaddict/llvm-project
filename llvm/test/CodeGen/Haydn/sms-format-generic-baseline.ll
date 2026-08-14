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
; isavail-delay stays default OFF; HANDOFF stays metrics-only / default OFF):
;   * PROD — product defaults
;   * GEN  — -haydn-premisched-matching-frontier=false (stock NodeOrder after
;            pressure/critical; cycle-ranking residual)
;   * RP   — -haydn-premisched-finer-rp-tracking=false (GenericScheduler
;            pressure-policy residual; AIE reduce_pressure peer surface)
;
; Frozen format-SMS KPIs (identical PROD/GEN/RP — unexplained delta blocks wave):
;   * dual-load MAC stream: soft_exit_ii_floor=4, format_resmii=3, port=4,
;     Res MII/II=4, rec=1, overestimate=0, exact_packable=1
;   * simple acc stream: soft_exit_ii_floor=3, format_resmii=2, port=3,
;     Res MII/II=3, rec=1, overestimate=0, exact_packable=1
;   * HOOK / RESMII / HANDOFF rejects silent; Schedule Found? 1 both kernels
;   * -stop-after=pipeliner: no durable BUNDLE (metrics-only HANDOFF freeze)
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
; BASE-NOT: SMS-RESMII: reject
; BASE-NOT: SMS-HANDOFF: reject

; --- PROD frozen ResMII/II/soft-exit (product ranking) ---
; PROD-DAG: SMS-RESMII: body_ops=7 greedy=3 exhaustive=3 overestimate=0
; PROD-DAG: SMS-HANDOFF: metrics-only freeze
; PROD-DAG: SMS-HANDOFF: qual-kernel body_ops=7 coissue_packable=0 exact_packable=1 exhaustive=3
; PROD-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=3 port_resmii=4 exact_packable=1 body_ops=7
; PROD-DAG: Return Res MII:4
; PROD-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; PROD-DAG: Schedule Found? 1 (II=4)
; PROD-DAG: SMS-RESMII: body_ops=4 greedy=2 exhaustive=2 overestimate=0
; PROD-DAG: SMS-HANDOFF: qual-kernel body_ops=4 coissue_packable=0 exact_packable=1 exhaustive=2
; PROD-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=4
; PROD-DAG: Return Res MII:3
; PROD-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; PROD-DAG: Schedule Found? 1 (II=3)

; --- GEN frozen ResMII/II/soft-exit (matching-frontier OFF residual) ---
; GEN-DAG: SMS-RESMII: body_ops=7 greedy=3 exhaustive=3 overestimate=0
; GEN-DAG: SMS-HANDOFF: metrics-only freeze
; GEN-DAG: SMS-HANDOFF: qual-kernel body_ops=7 coissue_packable=0 exact_packable=1 exhaustive=3
; GEN-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=3 port_resmii=4 exact_packable=1 body_ops=7
; GEN-DAG: Return Res MII:4
; GEN-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; GEN-DAG: Schedule Found? 1 (II=4)
; GEN-DAG: SMS-RESMII: body_ops=4 greedy=2 exhaustive=2 overestimate=0
; GEN-DAG: SMS-HANDOFF: qual-kernel body_ops=4 coissue_packable=0 exact_packable=1 exhaustive=2
; GEN-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=4
; GEN-DAG: Return Res MII:3
; GEN-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; GEN-DAG: Schedule Found? 1 (II=3)

; --- RP frozen ResMII/II/soft-exit (finer-rp OFF residual) ---
; RP-DAG: SMS-RESMII: body_ops=7 greedy=3 exhaustive=3 overestimate=0
; RP-DAG: SMS-HANDOFF: metrics-only freeze
; RP-DAG: SMS-HANDOFF: qual-kernel body_ops=7 coissue_packable=0 exact_packable=1 exhaustive=3
; RP-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=3 port_resmii=4 exact_packable=1 body_ops=7
; RP-DAG: Return Res MII:4
; RP-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; RP-DAG: Schedule Found? 1 (II=4)
; RP-DAG: SMS-RESMII: body_ops=4 greedy=2 exhaustive=2 overestimate=0
; RP-DAG: SMS-HANDOFF: qual-kernel body_ops=4 coissue_packable=0 exact_packable=1 exhaustive=2
; RP-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=4
; RP-DAG: Return Res MII:3
; RP-DAG: MII = 3 MAX_II = 13 (rec=1, res=3)
; RP-DAG: Schedule Found? 1 (II=3)

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
; POST: BUNDLE {{[01]}}
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
