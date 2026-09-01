; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-sms-containment-max=3 \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NS3
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-sms-containment-max=2 \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=CAP2
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs -debug-only=pipeliner \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PRODUCT
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -stop-after=pipeliner -debug-only=pipeliner \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PRODUCT2
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms-containment-max=3 \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms \
; RUN:     < %s -o %t.full.s 2>%t.rmk
; RUN: FileCheck %s --check-prefix=ASM-DRIVE < %t.full.s
; RUN: FileCheck %s --check-prefix=G005 < %t.rmk
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-sms-containment-max=3 -haydn-sms-force-pressure-reject \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PRESSURE
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mattr=+hwloop \
; RUN:     -haydn-enable-hwloops -haydn-enable-multistage-sms=false \
; RUN:     -O2 -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-sms-containment-max=3 \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ZOLFREE
; REQUIRES: asserts

; REGRESSION TEST (G012, 2026-08-23; W68.1 rebaseline 2026-08-25) — pre-RA
; <=3-stage soft pins for the -haydn-sms-containment-max F41 seam. G012 was
; originally a NO-ADOPT trial (knob-only, product containment=1); W68.1 makes
; generic pre-RA soft multi-stage PRODUCT (product bound = PPS-3 max-stage =
; 3), so the no-knob product arms now ACCEPT and the knob becomes a
; bisect-DOWN vehicle (lower values restore the historic Option A containment).
;
; Code truth this file locks (verified live before writing):
;   * The soft path admits NS<=3 soft schedules END-TO-END today: accept ->
;     classic ModuloScheduleExpander (guards + setPreheader + structural
;     adjustTripCount no-op) -> full codegen, -verify-machineinstrs clean.
;   * No other pre-RA gate blocks the path: analyzeLoopForPipelining hooks
;     (SMS-HOOK / RESMII / HANDOFF packability) pass for this body; PPS-3
;     stage gate HaydnSMSMaxStageCount=3 admits NS<=3; the W59 seam is
;     ZOL-only (soft never defers); ZOL containment stays pinned to 1.
;   * NS=4 is unreachable on constructible bodies: upstream -pipeliner-max-
;     stages (default 3) caps the search at MaxStageCount<=3 before
;     shouldUseSchedule ever sees a schedule, and PPS-3 max=3 rejects any
;     NS>3 that a raised upstream cap would surface. The cap mechanism is
;     therefore pinned at the boundary: NS=3 accepted at containment=3
;     (== product), REFUSED at containment=2 (same schedule, one lower knob
;     value).
;
; Safety rails pinned here (G012 scope items 3, W68.1-updated):
;   * PRESSURE — canAllocate/PPS-3 pressure pre-reject fires ON the accept
;     path (force-reject bisect vehicle, same seat as the product gate).
;   * Canonical remark — the post-RA LoopKPI remark still reports the loops
;     the pre-RA expansion left behind — D493 intact: pre-RA picked a
;     schedule, post-RA exact-commit gate re-analyzes from scratch and
;     declines the already-expanded shape ("not a single-BB ... candidate"
;     family / not-candidate kind). No pre-RA freeze crosses RA.
;   * Product arms (PRODUCT/PRODUCT2) — the same bodies at the product
;     bound now ACCEPT both NS=3 and NS=2 (W68.1: soft multi-stage is
;     product); the old Option A refuse-at-default is gone for soft loops.
;   * ZOL arm — under +hwloop these bodies become ZOL; the arm proves the
;     ZOL bound ignores the knob entirely (ZOL ownership stays pinned by
;     sms-f41-zol-containment-knobrefuse.ll, which lifts the knob on a real
;     ZOL body and still expects containment reject).

; NS=3 soft accept (ldchain: 4-deep dependent-load chain; organic swing
; schedule at -O2 finds stages=3 II=6). W68.1: knob=3 == product bound, so the
; accept names the product soft bound (not a lifted-trial label anymore).
; NS3: Schedule Found? 1 (II={{[1-9][0-9]*}})
; NS3: SMS-SHOULDUSE: accept stages=3 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; NS3-NOT: SMS-SHOULDUSE: reject multi-stage
; NS3: SMS-TC: soft adjustTripCount delta={{-?[0-9]+}} is a structural no-op

; Same body, cap one lower: the SAME NS=3 schedule is containment-refused
; (the mac2 NS=2 loop below still accepts — cap 2 admits NS<=2).
; This is the boundary pin standing in for NS=4-vs-cap-3 (NS=4 schedules do
; not exist under -pipeliner-max-stages=3; see header). The knob is now a
; BISECT-DOWN override (product default already admits NS<=3), so the mac2
; accept names the override label.
; CAP2: Schedule Found? 1 (II={{[1-9][0-9]*}})
; CAP2: SMS-SHOULDUSE: reject multi-stage stages=3 II={{[1-9][0-9]*}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; CAP2: SMS-SHOULDUSE: accept stages=2 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; containment-max override (bisect down))
; CAP2-NOT: SMS-SHOULDUSE: accept stages=3

; Product default (no knob): W68.1 — BOTH bodies' multi-stage soft schedules
; are now ACCEPTED (generic MachinePipeliner owns soft multi-stage up to the
; PPS-3 bound). ldchain NS=3 and mac2 NS=2 each accept; the old Option A
; containment that refused them is gone for soft loops.
; PRODUCT: Schedule Found? 1 (II={{[1-9][0-9]*}})
; PRODUCT: SMS-SHOULDUSE: accept stages=3 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; PRODUCT: SMS-SHOULDUSE: accept stages=2 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; PRODUCT-NOT: SMS-SHOULDUSE: reject multi-stage
; PRODUCT2: SMS-SHOULDUSE: accept stages=3 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; PRODUCT2: SMS-SHOULDUSE: accept stages=2 II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; PRODUCT2-NOT: SMS-SHOULDUSE: reject multi-stage

; Full-pipeline arm sanity (ldchain lifted expands; asm reachable).
; ASM-DRIVE: ldchain:

; G005 canonical per-loop remark on the lifted run: the post-RA host speaks
; about the loops the pre-RA expansion left — kind=not-candidate here (the
; already-expanded shape is not a single-BB ZOL/soft-countdown candidate).
; D493 intact: pre-RA picked a schedule, the post-RA gate re-analyzed from
; scratch and declined; no pre-RA freeze crossed RA. Both loops get exactly
; one canonical line (independent post-RA re-analysis per loop).
; G005: remark: <unknown>:0:0: rejected: not a single-BB ZOL/soft-countdown post-RA candidate
; G005: remark: <unknown>:0:0: Schedule found II={{[0-9]+}} NS={{[0-9]+}} prologue={{[0-9]+}} parcels epilogue={{[0-9]+}} parcels kind=not-candidate loop=bb.{{[0-9]+}}.loop
; G005: Schedule found II={{[0-9]+}} NS={{[0-9]+}} prologue={{[0-9]+}} parcels epilogue={{[0-9]+}} parcels kind=not-candidate loop=bb.{{[0-9]+}}.loop
; G005-NOT: kind=accepted

; Pressure rail fires on the lifted path (force-reject vehicle, product
; TrackRegPressure seat — same reject line as organic canAllocateSMS excess).
; PRESSURE: SMS-SHOULDUSE: reject pressure stages={{[1-9][0-9]*}} II={{[1-9][0-9]*}} (forced by -haydn-sms-force-pressure-reject)
; PRESSURE-NOT: SMS-SHOULDUSE: accept

; ZOL arm (+hwloop, knob lifted to 3): W68.1 — ZOL multi-stage is OWNED by
; the generic MachinePipeliner with the same form-uniform bound, so with the
; knob at 3 (== product) both ZOL bodies ACCEPT via their static
; MinTripCount(16) guard (AIE canAcceptII law). The knob at 1 restores the
; historic refuse for both forms; ZOL-specific law (single-stage reject,
; unknown-trip refuse) is pinned in HaydnHazardRecognizerTest
; (PreRASMSZOLMultiStageLiftPolarity) and sms-zol-multistage-containment.ll.
; ZOLFREE: SMS-SHOULDUSE: accept stages={{2|3}} II={{[1-9][0-9]*}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; ZOLFREE-NOT: SMS-SHOULDUSE: reject multi-stage

; NS=3 body: dependent-load chain (v_k = load(q + v_{k-1})), 4 loads deep.
define i32 @ldchain(ptr nocapture readonly %p, ptr nocapture readonly %q) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %ip = getelementptr inbounds i32, ptr %p, i32 %i
  %v0 = load i32, ptr %ip, align 4
  %w1 = getelementptr inbounds i32, ptr %q, i32 %v0
  %v1 = load i32, ptr %w1, align 4
  %w2 = getelementptr inbounds i32, ptr %q, i32 %v1
  %v2 = load i32, ptr %w2, align 4
  %w3 = getelementptr inbounds i32, ptr %q, i32 %v2
  %v3 = load i32, ptr %w3, align 4
  %s.next = add i32 %s, %v3
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 64
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %s.next
}

; NS=1-only body (dual-load MAC, ResMII-bound): organic schedule is
; single-stage under every knob value — the byte-identity arm. If the lifted
; knob ever perturbs loops it does not lift, cmp fails.
define i32 @mac2(ptr nocapture readonly %a, ptr nocapture readonly %b) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %mul = mul i32 %va, %vb
  %acc.next = add i32 %acc, %mul
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, 64
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc.next
}
