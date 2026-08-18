; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SMS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stop-after=pipeliner -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=HANDOFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stop-after=postmisched -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: MIR — soft-exit format-SMS QoR corpus (RecMII/II floors + post-RA
; exact-pack; no durable cycle invent before handoff).

; Format-SMS soft-exit QoR slice. Owns HaydnResourceCycle +
; analyzeLoopForPipelining SMS gates + this corpus. Does not own pre-RA
; list HR / productSoftExitIIFloor (sibling: prera-format-qor-exit.ll).
;
; Contracts:
; 1. Soft-exit II floor: softExitIIFloor = max(format exhaustive ResMII,
;    MI port lower bound). analyzeLoop logs SMS-QOR: soft_exit_ii_floor=…
;    Port-binds-above-format bodies still overestimate=0 and exact_packable=1.
; 2. RecMII floor: MAC acc→acc feedback remains latency-1 (DDG/itinerary).
;    Pipeliner reports (rec=1, res=…) — ResourceCycle never invents RecMII.
; 3. Post-RA exact-pack: qual-kernel exact_packable=1; after postmisched the
;    physical path may form multi-MI BUNDLE 0 (exact no-split). Pre-handoff
;    SMS expansion must NOT invent durable cycle groups (-stop-after=pipeliner
;    has no BUNDLE roots).
; 4. SMS-RESMII/HOOK/HANDOFF gates: no reject on these qualification bodies;
;    Schedule Found? 1 with II ≥ Rec/Res floors.
;
; Unit peer: HaydnBundleTest.SMSSoftExitQoRFloorsAndExactPack
; Companion RESMII/HOOK/port pins: sms-format-resmii*.mir
; RecMII peer: macc-acc-feedback-latency.ll
; Pre-RA sibling: prera-format-qor-exit.ll
; Dual-run generic-pass freeze (exact ResMII/II/soft-exit PROD/GEN/RP):
;   sms-format-generic-baseline.ll (VF3-G2; §8.4 #11)
; Dual-run ILP/critical residual attribution (PROD/GEN/RP KPI parity):
;   sms-format-ilp-crit-dual-run.ll (VF3-G3)

; SMS-NOT: SMS-HOOK: reject
; SMS-NOT: unsupported SMS-HOOK resource class
; SMS-NOT: Unable to analyzeLoop
; SMS-NOT: SMS-RESMII: reject
; SMS-NOT: SMS-HANDOFF: reject

; --- Dual-load MAC stream: RecMII floor (rec=1) + soft-exit II/exact-pack ---
; SMS-DAG: SMS-RESMII: body_ops={{[0-9]+}} greedy={{[0-9]+}} exhaustive={{[0-9]+}} overestimate=0
; SMS-DAG: SMS-FORMAT: rc_hr_diff match=1 {{.*}} pins=1
; SMS-DAG: SMS-HANDOFF: metrics-only freeze
; SMS-DAG: SMS-HANDOFF: qual-kernel body_ops={{[0-9]+}} coissue_packable={{[01]}} exact_packable=1 exhaustive={{[0-9]+}}
; SMS-DAG: SMS-QOR: soft_exit_ii_floor={{[1-9][0-9]*}} format_resmii={{[0-9]+}} port_resmii={{[0-9]+}} exact_packable=1 body_ops={{[0-9]+}}
; SMS-DAG: Return Res MII:{{[1-9][0-9]*}}
; SMS-DAG: MII = {{[0-9]+}} MAX_II = {{[0-9]+}} (rec=1, res={{[0-9]+}})
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})

; --- Simple acc stream: soft-exit II floor + exact-pack (ports may bind res) ---
; SMS-DAG: SMS-RESMII: body_ops={{[0-9]+}} greedy={{[0-9]+}} exhaustive={{[0-9]+}} overestimate=0
; SMS-DAG: SMS-FORMAT: rc_hr_diff match=1 {{.*}} pins=1
; SMS-DAG: SMS-HANDOFF: qual-kernel body_ops={{[0-9]+}} coissue_packable={{[01]}} exact_packable=1 exhaustive={{[0-9]+}}
; SMS-DAG: SMS-QOR: soft_exit_ii_floor={{[1-9][0-9]*}} format_resmii={{[0-9]+}} port_resmii={{[0-9]+}} exact_packable=1 body_ops={{[0-9]+}}
; SMS-DAG: Return Res MII:{{[1-9][0-9]*}}
; SMS-DAG: MII = {{[0-9]+}} MAX_II = {{[0-9]+}} (rec=1, res={{[0-9]+}})
; SMS-DAG: Schedule Found? 1 (II={{[1-9][0-9]*}})

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

; Dual-load MAC-shaped reduction — RecMII floor (acc feedback latency 1).
; Soft-exit II floor is max(format, ports); exact-pack metrics stay green.
; No HANDOFF invent after pipeliner; post-RA may exact-commit BUNDLE.
define i32 @qor_sms_mac_acc_feedback(ptr nocapture readonly %x,
                                     ptr nocapture readonly %h, i32 %n) {
; HANDOFF-LABEL: name: qor_sms_mac_acc_feedback
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|MULL|ADD32|ADDI32)}}
;
; POST-LABEL: name: qor_sms_mac_acc_feedback
; POST: BUNDLE {{[01]}}
;
; ASM-LABEL: qor_sms_mac_acc_feedback:
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

; Simple accumulation — analyzable SMS body with Res/Rec floors.
; Soft-exit II floor ≥ 1; exact_packable=1; still no pre-HANDOFF BUNDLE.
define i32 @qor_sms_acc_stream(ptr nocapture readonly %p, i32 %n) {
; HANDOFF-LABEL: name: qor_sms_acc_stream
; HANDOFF-NOT: BUNDLE
; HANDOFF-DAG: {{(LD32|S_LW|ADD32|ADDI32)}}
;
; POST-LABEL: name: qor_sms_acc_stream
; POST: S_LW_POST_IMM
;
; ASM-LABEL: qor_sms_acc_stream:
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
