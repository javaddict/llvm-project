; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs -debug-only=pipeliner \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms=false \
; RUN:     < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PRODUCT
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms=false \
; RUN:     < %s | FileCheck %s --check-prefix=POLICY
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -stop-after=pipeliner -verify-machineinstrs \
; RUN:     -haydn-sms-containment-max=1 -haydn-pipeliner-track-regpressure=false \
; RUN:     -debug-only=pipeliner < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=CONTAINED
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms=false \
; RUN:     < %s | FileCheck %s --check-prefix=POSTRA-ORD
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs \
; RUN:     -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.postra.rmk | FileCheck %s --check-prefix=POSTRA-SMS-ASM
; RUN: FileCheck %s --check-prefix=POSTRA-SMS < %t.postra.rmk
; RUN: not grep -q 'accepted II=' %t.postra.rmk || FileCheck %s --check-prefix=POSTRA-ACC < %t.postra.rmk
; REQUIRES: asserts

; REGRESSION TEST (W34 / F41, CR-H3) — containment ownership pins for the
; -haydn-sms-containment-max test knob introduced with the F41 soft
; adjustTripCount no-op fix (sms-f41-soft-tripcount-expander.mir owns the
; expander-path contract; ZOL lift-refusal is pinned by
; sms-f41-zol-containment-knobrefuse.ll).
;
; 2026-08-22 SMS product-default flip rebaseline: the SMS default is now
; ON, so every no-flag / explicit-false arm on this file pins the explicit
; OFF contract (-haydn-enable-multistage-sms=false); the POSTRA-SMS arm
; (explicit ON) now prints product-on. Product hardware-loop stays ON.
; The no-flag product-default pin lives in
; post-pipeliner-default-equals-off.ll (this body hangs past the
; generic pipeliner). Combined dual-ON qualify landed 2026-08-22 (G004);
; this file does not claim that matrix.
; Product / POLICY / LIFTED still stop after the generic pipeliner so the
; containment pin stays independent of RA. POSTRA-ORD / POSTRA-SMS run
; through RA + post-RA (T4 hang-root is capped): ordinary list-schedule
; commit completes with hwloops OFF, and SMS QUALIFY is exhaust/reject or
; parcels-per-iter == searched II. POLICY checks pre-RA MIR: no SET_HWLOOP
; and no SWPS metadata.
;
; Bug class guarded: the knob bisects the pre-RA SOFT StageCount containment
; bound (W68.1: product bound = PPS-3 max-stage, generic MachinePipeliner owns
; soft multi-stage). Three invariants:
;   1. PRODUCT (no flag): the soft StageCount>1 schedule is now ACCEPTED at
;      product defaults (W68.1: generic pre-RA multi-stage owns soft loops up
;      to the PPS-3 bound; ZOL stays contained). The body is the same
;      matrix_sum soft residual this file has always used.
;   2. POLICY (explicit enable flags false): pre-RA MIR is pipelined (soft
;      multi-stage accepted) but has no hardware-loop SET — ZOL is off and the
;      accepted soft schedule is bare logical MIs (no pre-RA cycle groups).
;   3. CONTAINED (containment=1, pressure gate off for decision determinism):
;      the knob restores the historic Option A single-stage soft containment,
;      so the organic soft multi-stage schedule is now REJECTED by
;      containment before any MIR mutation. The F41 bisect knob is still the
;      sole product-bound override (product policy no longer routes through
;      it). -stop-after keeps the run scoped to the pipeliner.
; If (1) fails the product soft multi-stage bound regressed; if (2) fails a
; ZOL/enable default flipped; if (3) fails the F41 knob no longer bisects the
; soft bound.

; PRODUCT: SMS-SHOULDUSE: accept stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (metrics-only; bare logical MIs; proven counted residual; no pre-RA cycle groups; product containment (PPS-3 bound))
; PRODUCT: SMS-TC: soft adjustTripCount delta={{-?[0-9]+}} is a structural no-op
; PRODUCT-NOT: SMS-SHOULDUSE: reject multi-stage
; PRODUCT-NOT: Reading virtual register without a def

; POLICY-NOT: SET_HWLOOP
; POLICY-NOT: swps
; POLICY: RET

; CONTAINED: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; CONTAINED-NOT: SMS-SHOULDUSE: accept
; CONTAINED-NOT: SMS-TC: soft adjustTripCount
;
; POSTRA-ORD: sms_f41_containment_pin:
; POSTRA-ORD: jalr
; POSTRA-ORD-NOT: set_hwloop
; POSTRA-ORD-NOT: #<swps>
;
; POSTRA-SMS-ASM: sms_f41_containment_pin:
; POSTRA-SMS-ASM: jalr
; POSTRA-SMS: {{accepted II=|exhausted:|rejected:}}
; POSTRA-SMS: qualify-or-cut
; POSTRA-SMS: product-on
; POSTRA-SMS: nat-ipc=measured-miss
; POSTRA-SMS: no-competitive-ipc
; POSTRA-SMS: no-stage0-ib-pp
; POSTRA-SMS: hwloop-combined=off
; POSTRA-SMS-NOT: sequential (preflight)
;
; POSTRA-ACC: accepted II=[[II:[0-9]+]]
; POSTRA-ACC-SAME: measured-II=[[II]]
; POSTRA-ACC: qualify parcels-per-iter=[[II]]
; POSTRA-ACC-SAME: searched-II=[[II]]

; CoreMark matrix_sum-like soft residual (proven to find a multi-stage
; schedule at -O2; same body as sms-multistage-naive-handoff-off-reject.ll).

define i32 @sms_f41_containment_pin(ptr nocapture readonly %C, i32 %N,
                                    i32 %clip) {
entry:
  %c0 = icmp eq i32 %N, 0
  br i1 %c0, label %exit, label %outer
outer:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %ret = phi i32 [ 0, %entry ], [ %ret.o, %outer.latch ]
  %prev = phi i32 [ 0, %entry ], [ %prev.o, %outer.latch ]
  %tmp0 = phi i32 [ 0, %entry ], [ %tmp.o, %outer.latch ]
  br label %inner
inner:
  %j = phi i32 [ 0, %outer ], [ %j.next, %inner ]
  %ret.i = phi i32 [ %ret, %outer ], [ %ret.next, %inner ]
  %prev.i = phi i32 [ %prev, %outer ], [ %cur, %inner ]
  %tmp.i = phi i32 [ %tmp0, %outer ], [ %tmp.next, %inner ]
  %idx = mul i32 %i, %N
  %idx2 = add i32 %idx, %j
  %p = getelementptr inbounds i32, ptr %C, i32 %idx2
  %cur = load i32, ptr %p, align 4
  %tmp.add = add i32 %tmp.i, %cur
  %gt = icmp sgt i32 %tmp.add, %clip
  %ret.a = add i32 %ret.i, 10
  %cmpcur = icmp sgt i32 %cur, %prev.i
  %one = zext i1 %cmpcur to i32
  %ret.b = add i32 %ret.i, %one
  %ret.next = select i1 %gt, i32 %ret.a, i32 %ret.b
  %tmp.next = select i1 %gt, i32 0, i32 %tmp.add
  %j.next = add nuw nsw i32 %j, 1
  %cond = icmp eq i32 %j.next, %N
  br i1 %cond, label %outer.latch, label %inner
outer.latch:
  %ret.o = phi i32 [ %ret.next, %inner ]
  %prev.o = phi i32 [ %cur, %inner ]
  %tmp.o = phi i32 [ %tmp.next, %inner ]
  %i.next = add nuw nsw i32 %i, 1
  %ocond = icmp eq i32 %i.next, %N
  br i1 %ocond, label %exit, label %outer
exit:
  %r = phi i32 [ 0, %entry ], [ %ret.o, %outer.latch ]
  ret i32 %r
}
