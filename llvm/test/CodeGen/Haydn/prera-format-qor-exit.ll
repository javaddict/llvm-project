; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -stop-before=greedy -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=PREGREEDY
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -stop-before=postmisched -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=PREPOST
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -stop-after=postmisched -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=POST
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -stats -verify-machineinstrs < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=STATS
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=ASM

; Role: MIR — soft-exit pre-RA QoR corpus (RecMII/II floors + post-RA
; exact-pack correlation; no durable cycle invent before handoff).

; Pre-RA soft-exit QoR slice. Owns list-sched + CreateTargetMIHazardRecognizer
; (IsPreRA) + PortModel floors + matching-frontier tryCandidate. Does not own
; SMS ResourceCycle / analyzeLoop / MachinePipeliner hooks (sibling:
; sms-format-qor-exit / sms-format-resmii*).
;
; Contracts:
; 1. -stop-before=greedy: pre-RA list-sched emits only logical opcodes — no
;    private member setDesc (_S0/_S1/_S2), no FormatID stamp, no durable BUNDLE
;    roots invented from matching-frontier scores (phase identity).
; 2. -stop-before=postmisched: through RA the same kernels stay logical — still
;    no pre-handoff hard BUNDLE (handoff metrics-only freeze).
; 3. -stop-after=postmisched: post-RA exact no-split owns multi-MI BUNDLE 0
;    commit for packable independent sets (pre-RA only proved packability via
;    productQualKernel* oracles; unit: PreRASoftExitQoRFloorsAndExactPack).
; 4. -stats: multi-MI exact finalize > 0; scheduled split + unstamped hard-root
;    counters stay silent (zero).
; 5. II / Res floors (pre-RA surface): port ResMII binds when format-only is 1
;    (3×1W GPR writes → soft-exit II floor ≥ 2 via productSoftExitIIFloor). Rec
;    floors for loop-carried MAC acc feedback remain SMS/DDG (macc-acc-feedback);
;    pre-RA does not invent RecMII.
; 6. Pressure / critical path stay primary under matching-frontier ranking
;    (scheduler-ilp.ll / scheduler-critical-path.ll peers).
;
; Companion unit: HaydnPortModelTest.PreRASoftExitQoRFloorsAndExactPack
; Companion MIR: prera-format-feasibility.mir (vreg ports / three-ready rematch)
; Dual-run generic-pass baseline (VF3-G2): prera-format-generic-baseline.ll

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

; Independent ALU triple — pre-RA logical only; post-RA may exact-commit a
; multi-MI Full cycle (ports may pack 2-wide under 4R2W). Soft-exit II floor
; unit-pins max(format=1, port for full 3×2R1W=2) without inventing HANDOFF.
define i32 @qor_pack_three_alu(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
; PREGREEDY-LABEL: name: qor_pack_three_alu
; PREGREEDY-NOT: BUNDLE
; PREGREEDY-DAG: ADD32
; PREGREEDY-DAG: XOR32
; PREGREEDY-DAG: OR32
; PREGREEDY-NOT: ADD32_S
; PREGREEDY-NOT: XOR32_S
; PREGREEDY-NOT: OR32_S
;
; PREPOST-LABEL: name: qor_pack_three_alu
; PREPOST-NOT: BUNDLE
; PREPOST-DAG: ADD32
; PREPOST-DAG: XOR32
; PREPOST-DAG: OR32
; PREPOST-NOT: ADD32_S
;
; POST-LABEL: name: qor_pack_three_alu
; POST: BUNDLE 0
; POST-DAG: ADD32
; POST-DAG: XOR32
;
; ASM-LABEL: qor_pack_three_alu:
; ASM-DAG: add32
; ASM-DAG: xor32
; ASM-DAG: or32
entry:
  %x = add i32 %a, %b
  %y = xor i32 %c, %d
  %z = or i32 %e, %f
  %t0 = add i32 %x, %y
  %t1 = add i32 %t0, %z
  ret i32 %t1
}

; Port-forced II floor body: three independent live results from three
; distinct memory sources (defeats IR fold of a+10/a+20/a+30 → 3a+60).
; Pre-RA HR (IsPreRA + PortModel) charges multi-cycle under 2W for dense
; write sets — still logical-only at stop-before greedy/postmisched. Unit:
; productSoftExitIIFloor({ADD32×3}, GPRW=3) == 2
; (HaydnPortModelTest.PreRASoftExitQoRFloorsAndExactPack).
define i32 @qor_three_write_port_floor(ptr nocapture readonly %p,
                                       ptr nocapture readonly %q,
                                       ptr nocapture readonly %r) {
; PREGREEDY-LABEL: name: qor_three_write_port_floor
; PREGREEDY-NOT: BUNDLE
; PREGREEDY-DAG: LD32
; PREGREEDY-DAG: ADD32
; PREGREEDY-NOT: LD32_S
; PREGREEDY-NOT: ADD32_S
;
; PREPOST-LABEL: name: qor_three_write_port_floor
; PREPOST-NOT: BUNDLE
; PREPOST-NOT: LD32_S
; PREPOST-NOT: ADD32_S
;
; POST-LABEL: name: qor_three_write_port_floor
; POST: BUNDLE 0
;
; ASM-LABEL: qor_three_write_port_floor:
; ASM-DAG: ld32
; ASM-DAG: add32
entry:
  %x = load i32, ptr %p, align 4
  %y = load i32, ptr %q, align 4
  %z = load i32, ptr %r, align 4
  %t0 = add i32 %x, %y
  %t1 = add i32 %t0, %z
  ret i32 %t1
}

; Streaming dual-load MAC-shaped kernel — pre-RA through RA stays logical
; (no HANDOFF invent). Post-RA exact-pack owns BUNDLE. RecMII for acc feedback
; is SMS-owned (macc-acc-feedback / sms-format-qor-exit); here pin only that
; pre-RA list + RA never stamp members or invent cycle groups.
define i32 @qor_dual_load_mac_stream(ptr nocapture readonly %x,
                                     ptr nocapture readonly %h, i32 %n) {
; PREGREEDY-LABEL: name: qor_dual_load_mac_stream
; PREGREEDY-NOT: BUNDLE
; PREGREEDY-NOT: {{LD32|MUL|ADD32}}_S
;
; PREPOST-LABEL: name: qor_dual_load_mac_stream
; PREPOST-NOT: BUNDLE
;
; POST-LABEL: name: qor_dual_load_mac_stream
; POST: BUNDLE 0
;
; ASM-LABEL: qor_dual_load_mac_stream:
; ASM: // =>This Inner Loop Header: Depth=1
; ASM-DAG: {{(ld32|s_lw|mull|mul64|add32)}}
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
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
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

; Critical-path + independent side work — pressure/critical stay primary
; under matching-frontier tryCandidate; pre-RA never member-stamps.
define i32 @qor_critical_and_side(i32 %a, i32 %b, i32 %c) {
; PREGREEDY-LABEL: name: qor_critical_and_side
; PREGREEDY-NOT: BUNDLE
; PREGREEDY-DAG: ADD32
; PREGREEDY-NOT: ADD32_S
;
; PREPOST-LABEL: name: qor_critical_and_side
; PREPOST-NOT: BUNDLE
;
; POST-LABEL: name: qor_critical_and_side
; POST: BUNDLE 0
;
; ASM-LABEL: qor_critical_and_side:
; ASM-DAG: add32
entry:
  ; Longer chain on %a (critical).
  %a1 = add i32 %a, 1
  %a2 = add i32 %a1, 2
  %a3 = add i32 %a2, 3
  ; Independent side work on %b/%c (ILP / packing).
  %s = xor i32 %b, %c
  %t = or i32 %b, %c
  %u = add i32 %s, %t
  %r = add i32 %a3, %u
  ret i32 %r
}

; Soft-exit stats: multi-MI exact finalize fires on qual kernels; the
; 'failed exact no-split' counter may be zero under multi-stage SMS containment (the post-RA
; field-order-RAW fix, companion regression postra-field-order-raw-narrow-store.mir,
; correctly fails commit for cycles whose field-order permutation would create a
; no-forwarding RAW hazard on adjacent narrow stores; such cycles fall back to
; sequential parcels — correct product behavior, not a regression); unstamped
; hard-root counter stays at zero (silent in -stats).
; STATS: multi-MI cycles finalized as BUNDLE
; STATS-NOT: unstamped multi-member hard BUNDLE roots at post-RA
