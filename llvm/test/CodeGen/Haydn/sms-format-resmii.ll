; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -debug-only=pipeliner -verify-machineinstrs < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SMS
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:   -O2 -stop-after=pipeliner -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=HANDOFF

; Role: MIR — SMS slice (vliw-format-aware-scheduling-pipeline.md §5.2 / §8.3): * HaydnResourceCycle same-cycle format + descriptor-derived port demand.

; SMS slice (vliw-format-aware-scheduling-pipeline.md §5.2 / §8.3):
;   * HaydnResourceCycle same-cycle format + descriptor-derived port demand
;   * SMS-HOOK fail-closed for unsupported class-3 / operand-dependent cases
;     (ordinary countable loops remain analyzable — multi-cycle stages absent)
;   * SMS-RESMII: calculateResMIIDFA still owns the SMS starting estimate; the
;     target compares greedy vs exhaustive ≤3 format oracle and fail-closes on
;     positive overestimate (see sms-format-resmii-overestimate-reject.mir).
;     These bodies have overestimate=0. No shared MachinePipeliner rewrite.
;   * Port-forced ResMII ≥ 2 pin: sms-format-resmii-port-forced.mir
;   * SMS-HANDOFF metrics-only freeze: analyzeLoop logs qual-kernel post-RA
;     packability; recordSuccessfulSMS stores scalar SWPS only.
;     Default OFF: -stop-after=pipeliner keeps logical opcodes only
;     (HANDOFF-NOT:BUNDLE). Opt-in: -haydn-sms-handoff
;     (sms-handoff-bundle-through-ra.mir).
;
; Port model: three independent GPR writes cannot share one cycle (2W cap)
; even when Full format has three slots — ResourceCycle ports bind ResMII /
; placement. Ordinary streaming kernels with dual-load + MAC stay placeable.
; Companion MIR: sms-format-resmii.mir (vreg body / ResMII / HANDOFF).
;
; SMS-NOT: SMS-HOOK: reject
; SMS-NOT: unsupported SMS-HOOK resource class
; SMS-NOT: Unable to analyzeLoop
; SMS-NOT: SMS-RESMII: reject
; SMS-NOT: SMS-HANDOFF: reject
; Both kernels remain analyzable; ResMII ≥ 2; SMS finds a schedule (II varies).
; Zero SMS-RESMII overestimate (greedy ≡ exhaustive on qualification bodies).
; SMS-DAG: SMS-RESMII: body_ops={{[0-9]+}} greedy={{[0-9]+}} exhaustive={{[0-9]+}} overestimate=0
; SMS-DAG: SMS-FORMAT: rc_hr_diff match=1 {{.*}} pins=1
; SMS-DAG: SMS-HANDOFF: metrics-only freeze
; SMS-DAG: SMS-HANDOFF: qual-kernel body_ops={{[0-9]+}} coissue_packable={{[01]}} exact_packable=1 exhaustive={{[0-9]+}}
; SMS-DAG: Return Res MII:{{[2-9]|[1-9][0-9]+}}
; SMS-DAG: Schedule Found? 1 (II={{[0-9]+}})
; SMS-DAG: SMS-RESMII: body_ops={{[0-9]+}} greedy={{[0-9]+}} exhaustive={{[0-9]+}} overestimate=0
; SMS-DAG: SMS-FORMAT: rc_hr_diff match=1 {{.*}} pins=1
; SMS-DAG: SMS-HANDOFF: metrics-only freeze
; SMS-DAG: SMS-HANDOFF: qual-kernel body_ops={{[0-9]+}} coissue_packable={{[01]}} exact_packable=1 exhaustive={{[0-9]+}}
; SMS-DAG: Return Res MII:{{[2-9]|[1-9][0-9]+}}
; SMS-DAG: Schedule Found? 1 (II={{[0-9]+}})

; Simple accumulation: analyzable; SMS finds a schedule at MII.

define i32 @sms_acc_port_shape(ptr nocapture readonly %p, i32 %n) {
; ASM-LABEL: sms_acc_port_shape:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM:        add32
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; Dual independent streams + MAC-shaped body: descriptor ports + format
; must still analyze (LD/LD/MAC packs under Full when ports allow).
define i32 @sms_dual_load_mac_shape(ptr nocapture readonly %x,
                                    ptr nocapture readonly %h, i32 %n) {
; ASM-LABEL: sms_dual_load_mac_shape:
; ASM:        // =>This Inner Loop Header: Depth=1
; ASM-DAG:    {{(ld32|s_lw|mull|mul64|add32)}}
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %hi = getelementptr i32, ptr %h, i32 %i
  %xv = load i32, ptr %xi
  %hv = load i32, ptr %hi
  %prod = mul i32 %xv, %hv
  %acc.next = add i32 %acc, %prod
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %acc.next
}

; SMS-HANDOFF negative: after pipeliner, kernel ops remain logical opcodes —
; no pre-RA BUNDLE cycle group from SMS expansion (durable groups need
; approved handoff + VF4 RA preservation).
; HANDOFF-LABEL: name: sms_acc_port_shape
; HANDOFF-NOT: BUNDLE
; HANDOFF: ADD32
; HANDOFF-LABEL: name: sms_dual_load_mac_shape
; HANDOFF-NOT: BUNDLE
