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
; RUN:     -haydn-sms-containment-max=3 -haydn-pipeliner-track-regpressure=false \
; RUN:     -debug-only=pipeliner < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LIFTED
; REQUIRES: asserts

; REGRESSION TEST (W34 / F41, CR-H3) — containment ownership pins for the
; -haydn-sms-containment-max test knob introduced with the F41 soft
; adjustTripCount no-op fix (sms-f41-soft-tripcount-expander.mir owns the
; expander-path contract; ZOL lift-refusal is pinned by
; sms-f41-zol-containment-knobrefuse.ll).
;
; Product hardware-loop and multi-stage flags stay OFF on this file.
; The no-flag product-default pin lives in
; post-pipeliner-default-equals-off.ll (this body hangs past the
; generic pipeliner). Combined dual-ON qualify waits for T2 then T5
; then this matrix; this file does not claim that matrix closed.
; All three arms stop after the generic pipeliner: RA/post-RA hangs on
; this body (T4 hang-root) must not mask the containment pin. POLICY
; checks pre-RA MIR: no SET_HWLOOP and no SWPS metadata.
;
; Bug class guarded: the knob exists ONLY to drive a found soft multi-stage
; schedule through the classic expander for lit verification. Three invariants:
;   1. PRODUCT (no flag): soft StageCount>1 is still containment-rejected
;      before any MIR mutation (Option A law; pre-RA multi-stage is post-RA
;      only). Same body as sms-multistage-naive-handoff-off-reject.ll, which
;      pins this reject today — this file adds the lifted contrast arm.
;   2. POLICY (explicit enable flags false): pre-RA MIR has no hardware-loop
;      SET and no multi-stage SWPS annotation. If this fails a product
;      default flipped ON.
;   3. LIFTED (containment=3, pressure gate off for decision determinism):
;      the organic soft multi-stage schedule is ACCEPTED and driven through
;      the classic expander; the F41 soft adjustTripCount no-op line fires;
;      -verify-machineinstrs stays clean (no dangling vreg from an adjusted
;      trip def inserted into the MBB the expander erases). -stop-after keeps
;      the run scoped to the pipeliner (the expander contract under test).
; If (1) fails the knob became product policy; if (2) fails a product
; default flipped; if (3) fails the F41 fix or the expander soft path
; regressed.

; PRODUCT: SMS-SHOULDUSE: reject multi-stage stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}} (pre-RA StageCount>1 containment; post-RA multi-stage only)
; PRODUCT-NOT: SMS-SHOULDUSE: accept

; POLICY-NOT: SET_HWLOOP
; POLICY-NOT: swps
; POLICY: RET

; LIFTED-NOT: SMS-SHOULDUSE: reject multi-stage
; LIFTED: SMS-SHOULDUSE: accept stages={{[2-9]|[1-9][0-9]+}} II={{[0-9]+}}
; LIFTED: SMS-TC: soft adjustTripCount delta={{-?[0-9]+}} is a structural no-op
; LIFTED-NOT: Reading virtual register without a def

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
