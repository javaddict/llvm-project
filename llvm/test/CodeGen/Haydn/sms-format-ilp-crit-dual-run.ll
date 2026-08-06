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

; Role: MIR — VF3-G3-ILP-CRIT-DUAL-RUN SMS slice. Dual-run ranking residual
; attribution on ILP / critical-path SMS qualification kernels.
; Owns HaydnResourceCycle + analyzeLoopForPipelining SMS gates + this corpus.
; Does NOT own pre-RA list HR / PreRASchedStrategy / matching-frontier
; implementation (sibling: scheduler-ilp.ll / scheduler-critical-path.ll dual-run
; + prera-format-generic-baseline.ll).

; Full-only product ranking residual must not unexplainedly change format-SMS
; KPIs on dedicated ILP and critical-path loop bodies. G2
; sms-format-generic-baseline.ll freezes MAC/acc streams; this file freezes
; ILP (independent multi-load + dual-acc) and critical-path (dependent chain
; + independent side work) peers under the same PROD/GEN/RP dual-run arms.
;
; Dual-run arms (existing product flags only; never null pre-RA HR factory;
; isavail-delay stays default OFF; HANDOFF stays metrics-only / default OFF):
;   * PROD — product defaults (matching-frontier ON, finer-rp ON)
;   * GEN  — -haydn-premisched-matching-frontier=false
;            (stock NodeOrder after pressure/critical; cycle-ranking residual)
;   * RP   — -haydn-premisched-finer-rp-tracking=false
;            (GenericScheduler pressure-policy residual)
;
; Frozen format-SMS KPIs (identical PROD/GEN/RP — unexplained delta blocks wave):
;   * ILP triple-load stream: soft_exit_ii_floor=6, format_resmii=4, port=6,
;     Res MII/II=6, rec=1, overestimate=0, exact_packable=1, body_ops=10
;   * critical chain + side: soft_exit_ii_floor=4, format_resmii=2, port=4,
;     Res MII/II=4, rec=1, overestimate=0, exact_packable=1, body_ops=6
;   * ILP dual-acc stream: soft_exit_ii_floor=3, format_resmii=2, port=3,
;     Res MII/II=4 (D999 no-forwarding: load->{add,xor} RAW pairs can no
;     longer share a cycle in calculateResMIIDFA's MI overload, so DFA ResMII
;     rose 3->4 while port_resmii stays 3), rec=1, exact_packable=1, body_ops=5
;   * HOOK / RESMII / HANDOFF rejects silent; Schedule Found? 1 all kernels
;   * -stop-after=pipeliner: no durable BUNDLE (metrics-only HANDOFF freeze)
;   * -stop-after=postmisched: multi-MI BUNDLE 0 still legal both ranking modes
;   * -stats attribution: PROD fires ResourceDemand pre-RA; GEN/RP residual
;     arms have none; multi-MI exact finalize parity; spill/split/hard-root silent
;
; Soft-exit product-only peer: sms-format-qor-exit.ll
; G2 dual-run generic peer: sms-format-generic-baseline.ll
; Unit peer: HaydnBundleTest.SMSSoftExitQoRFloorsAndExactPack
; Pre-RA sibling dual-run: scheduler-ilp.ll / scheduler-critical-path.ll
;                          + prera-format-generic-baseline.ll

; --- Shared fail-closed silence (all ranking arms) ---
; BASE-NOT: SMS-HOOK: reject
; BASE-NOT: unsupported SMS-HOOK resource class
; BASE-NOT: Unable to analyzeLoop
; BASE-NOT: SMS-RESMII: reject
; BASE-NOT: SMS-HANDOFF: reject

; --- PROD frozen ResMII/II/soft-exit (product ranking) ---
; PROD-DAG: SMS-RESMII: body_ops=10 greedy=4 exhaustive=4 overestimate=0
; PROD-DAG: SMS-HANDOFF: metrics-only freeze
; PROD-DAG: SMS-HANDOFF: qual-kernel body_ops=10 coissue_packable=0 exact_packable=1 exhaustive=4
; PROD-DAG: SMS-QOR: soft_exit_ii_floor=6 format_resmii=4 port_resmii=6 exact_packable=1 body_ops=10
; PROD-DAG: Return Res MII:6
; PROD-DAG: MII = 6 MAX_II = 16 (rec=1, res=6)
; PROD-DAG: Schedule Found? 1 (II=6)
; PROD-DAG: SMS-RESMII: body_ops=6 greedy=2 exhaustive=2 overestimate=0
; PROD-DAG: SMS-HANDOFF: qual-kernel body_ops=6 coissue_packable=0 exact_packable=1 exhaustive=2
; PROD-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=2 port_resmii=4 exact_packable=1 body_ops=6
; PROD-DAG: Return Res MII:4
; PROD-DAG: MII = 4 MAX_II = 14 (rec=2, res=4)
; PROD-DAG: Schedule Found? 1 (II=4)
; PROD-DAG: SMS-RESMII: body_ops=5 greedy=2 exhaustive=2 overestimate=0
; PROD-DAG: SMS-HANDOFF: qual-kernel body_ops=5 coissue_packable=0 exact_packable=1 exhaustive=2
; PROD-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=5
; PROD-DAG: Return Res MII:4
; PROD-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; PROD-DAG: Schedule Found? 1 (II=4)

; --- GEN frozen ResMII/II/soft-exit (matching-frontier OFF residual) ---
; GEN-DAG: SMS-RESMII: body_ops=10 greedy=4 exhaustive=4 overestimate=0
; GEN-DAG: SMS-HANDOFF: metrics-only freeze
; GEN-DAG: SMS-HANDOFF: qual-kernel body_ops=10 coissue_packable=0 exact_packable=1 exhaustive=4
; GEN-DAG: SMS-QOR: soft_exit_ii_floor=6 format_resmii=4 port_resmii=6 exact_packable=1 body_ops=10
; GEN-DAG: Return Res MII:6
; GEN-DAG: MII = 6 MAX_II = 16 (rec=1, res=6)
; GEN-DAG: Schedule Found? 1 (II=6)
; GEN-DAG: SMS-RESMII: body_ops=6 greedy=2 exhaustive=2 overestimate=0
; GEN-DAG: SMS-HANDOFF: qual-kernel body_ops=6 coissue_packable=0 exact_packable=1 exhaustive=2
; GEN-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=2 port_resmii=4 exact_packable=1 body_ops=6
; GEN-DAG: Return Res MII:4
; GEN-DAG: MII = 4 MAX_II = 14 (rec=2, res=4)
; GEN-DAG: Schedule Found? 1 (II=4)
; GEN-DAG: SMS-RESMII: body_ops=5 greedy=2 exhaustive=2 overestimate=0
; GEN-DAG: SMS-HANDOFF: qual-kernel body_ops=5 coissue_packable=0 exact_packable=1 exhaustive=2
; GEN-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=5
; GEN-DAG: Return Res MII:4
; GEN-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; GEN-DAG: Schedule Found? 1 (II=4)

; --- RP frozen ResMII/II/soft-exit (finer-rp OFF residual) ---
; RP-DAG: SMS-RESMII: body_ops=10 greedy=4 exhaustive=4 overestimate=0
; RP-DAG: SMS-HANDOFF: metrics-only freeze
; RP-DAG: SMS-HANDOFF: qual-kernel body_ops=10 coissue_packable=0 exact_packable=1 exhaustive=4
; RP-DAG: SMS-QOR: soft_exit_ii_floor=6 format_resmii=4 port_resmii=6 exact_packable=1 body_ops=10
; RP-DAG: Return Res MII:6
; RP-DAG: MII = 6 MAX_II = 16 (rec=1, res=6)
; RP-DAG: Schedule Found? 1 (II=6)
; RP-DAG: SMS-RESMII: body_ops=6 greedy=2 exhaustive=2 overestimate=0
; RP-DAG: SMS-HANDOFF: qual-kernel body_ops=6 coissue_packable=0 exact_packable=1 exhaustive=2
; RP-DAG: SMS-QOR: soft_exit_ii_floor=4 format_resmii=2 port_resmii=4 exact_packable=1 body_ops=6
; RP-DAG: Return Res MII:4
; RP-DAG: MII = 4 MAX_II = 14 (rec=2, res=4)
; RP-DAG: Schedule Found? 1 (II=4)
; RP-DAG: SMS-RESMII: body_ops=5 greedy=2 exhaustive=2 overestimate=0
; RP-DAG: SMS-HANDOFF: qual-kernel body_ops=5 coissue_packable=0 exact_packable=1 exhaustive=2
; RP-DAG: SMS-QOR: soft_exit_ii_floor=3 format_resmii=2 port_resmii=3 exact_packable=1 body_ops=5
; RP-DAG: Return Res MII:4
; RP-DAG: MII = 4 MAX_II = 14 (rec=1, res=4)
; RP-DAG: Schedule Found? 1 (II=4)

; --- Dual-run -stats attribution (PROD vs GEN vs RP) ---
; FileCheck order follows -stats emission (post-RA first among these).
; Ranking residual attribution: product alone records ResourceDemand pre-RA
; picks (matching-frontier); GEN/RP residual arms have none. Multi-MI exact
; finalize parity on every arm; split/hard-root/spill silent.
; STATS-PROD: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-PROD: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-PROD-NOT: failed exact no-split
; STATS-PROD-NOT: multi-member hard BUNDLE roots at post-RA
; STATS-PROD-NOT: Number of spills inserted
; STATS-PROD-NOT: Number of reloads inserted
;
; STATS-GEN: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-GEN-NOT: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-GEN-NOT: failed exact no-split
; STATS-GEN-NOT: multi-member hard BUNDLE roots at post-RA
; STATS-GEN-NOT: Number of spills inserted
; STATS-GEN-NOT: Number of reloads inserted
;
; STATS-RP: haydn-post-ra-sched{{.*}}multi-MI cycles finalized as BUNDLE
; STATS-RP-NOT: machine-scheduler{{.*}}ResourceDemand heuristic pre-RA
; STATS-RP-NOT: failed exact no-split
; STATS-RP-NOT: multi-member hard BUNDLE roots at post-RA
; STATS-RP-NOT: Number of spills inserted
; STATS-RP-NOT: Number of reloads inserted

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

; ILP: three independent loads + reduction in a countable SMS body.
; Soft-exit II floor = max(format=4, ports=6) = 6; RecMII stays DDG (rec=1).
; Ranking residual must not change Res/II/soft-exit under dual-run arms.
define i32 @sms_ilp_independent_loads(ptr nocapture readonly %p1,
                                      ptr nocapture readonly %p2,
                                      ptr nocapture readonly %p3, i32 %n) {
; HANDOFF-LABEL: name: sms_ilp_independent_loads
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|ADD32|ADDI32)}}
; HANDOFF-NOT: {{(LD32|S_LW|ADD32)}}_S
;
; POST-LABEL: name: sms_ilp_independent_loads
; POST: BUNDLE 0
;
; ASM-LABEL: sms_ilp_independent_loads:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|add32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %a1 = getelementptr i32, ptr %p1, i32 %i
  %a2 = getelementptr i32, ptr %p2, i32 %i
  %a3 = getelementptr i32, ptr %p3, i32 %i
  %v1 = load i32, ptr %a1, align 4
  %v2 = load i32, ptr %a2, align 4
  %v3 = load i32, ptr %a3, align 4
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %acc.next = add i32 %acc, %s2
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %acc.next
}

; Critical path: long dependent add chain on the load + independent side XOR.
; Soft-exit II floor = max(format=2, ports=4) = 4; pressure/critical primacy
; on the pre-RA sibling must not invent SMS KPI deltas under residual arms.
define i32 @sms_critical_path_chain(ptr nocapture readonly %p, i32 %n) {
; HANDOFF-LABEL: name: sms_critical_path_chain
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|ADD32|XOR32|ADDI32)}}
; HANDOFF-NOT: {{(LD32|S_LW|ADD32|XOR32)}}_S
;
; POST-LABEL: name: sms_critical_path_chain
; POST: BUNDLE 0
;
; ASM-LABEL: sms_critical_path_chain:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|add32|xor32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %side = phi i32 [ 0, %entry ], [ %side.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %a1 = add i32 %v, 1
  %a2 = add i32 %a1, 2
  %a3 = add i32 %a2, 3
  %acc.next = add i32 %acc, %a3
  %side.next = xor i32 %side, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  %r = add i32 %acc.next, %side.next
  ret i32 %r
}

; ILP ALU dual-accumulator stream — two independent reductions over one load.
; Soft-exit II floor = max(format=2, ports=3) = 3 under all ranking arms.
define i32 @sms_ilp_dual_acc(ptr nocapture readonly %p, i32 %n) {
; HANDOFF-LABEL: name: sms_ilp_dual_acc
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|ADD32|XOR32|ADDI32)}}
; HANDOFF-NOT: {{(LD32|S_LW|ADD32|XOR32)}}_S
;
; POST-LABEL: name: sms_ilp_dual_acc
; POST: BUNDLE 0
;
; ASM-LABEL: sms_ilp_dual_acc:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|add32|xor32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %a = phi i32 [ 0, %entry ], [ %a.next, %loop ]
  %b = phi i32 [ 0, %entry ], [ %b.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %pi, align 4
  %a.next = add i32 %a, %v
  %b.next = xor i32 %b, %v
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  %r = add i32 %a.next, %b.next
  ret i32 %r
}
