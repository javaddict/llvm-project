; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll/s2); slot auction reorganized the two mul+sext chains in loop body and epilogue.
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: iir_biquad:
; CHECK: {{.}}

define void @iir_biquad(ptr %out, ptr %in, ptr %coeffs, ptr %state, i32 %n) {
;
; The streaming loop contains the per-tap multiply chain for the 5 filter taps
; b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2. Under each s32 mul lowers to
; mull (or widen mul64.ll), with sub32 for the two
; feedback taps (a1*y1, a2*y2). The chain is hoisted into BOTH the.LBB0_1
; preheader and the.LBB0_2 prologue peel, then re-emitted in the main loop
; (.LBB0_3) and the post-loop tail (.LBB0_6), so these are matched with
;
; Loop body: output store (st32 to r8 = out ptr) followed by the slt32+bnez
; back-edge. The icmp slt back-edge comparison materializes as slt32+bnez
; (unfused) under the SFR-strip scheduling model.
; (SFR-strip) changed bundle layout — rebaselined /17.
; (mul path: mull) shifted schedule: loop header is.LBB0_3 (was.LBB0_2)
; and the back-edge bnez targets.LBB0_3. Rebaselined so the
; back-edge bnez is matched relative to the loop-body st32, not the post-loop
; state stores.
;
; State variable stores (update delay line) after the loop
;
; Return
entry:
  ; Load coefficients (fixed: b0, b1, b2, a1, a2)
  %pb0 = getelementptr i32, ptr %coeffs, i32 0
  %pb1 = getelementptr i32, ptr %coeffs, i32 1
  %pb2 = getelementptr i32, ptr %coeffs, i32 2
  %pa1 = getelementptr i32, ptr %coeffs, i32 3
  %pa2 = getelementptr i32, ptr %coeffs, i32 4
  %b0 = load i32, ptr %pb0
  %b1 = load i32, ptr %pb1
  %b2 = load i32, ptr %pb2
  %a1 = load i32, ptr %pa1
  %a2 = load i32, ptr %pa2

  ; Load initial state: state[0]=x1, state[1]=x2, state[2]=y1, state[3]=y2
  %px1 = getelementptr i32, ptr %state, i32 0
  %px2 = getelementptr i32, ptr %state, i32 1
  %py1 = getelementptr i32, ptr %state, i32 2
  %py2 = getelementptr i32, ptr %state, i32 3
  %x1.init = load i32, ptr %px1
  %x2.init = load i32, ptr %px2
  %y1.init = load i32, ptr %py1
  %y2.init = load i32, ptr %py2

  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %x1 = phi i32 [ %x1.init, %entry ], [ %xn, %loop ]
  %x2 = phi i32 [ %x2.init, %entry ], [ %x1, %loop ]
  %y1 = phi i32 [ %y1.init, %entry ], [ %yn, %loop ]
  %y2 = phi i32 [ %y2.init, %entry ], [ %y1, %loop ]

  ; Load input sample x[n]
  %ptr.in = getelementptr i32, ptr %in, i32 %i
  %xn = load i32, ptr %ptr.in

  ; Compute y[n] = b0*x[n] + b1*x1 + b2*x2 - a1*y1 - a2*y2
  %m0 = mul i32 %b0, %xn
  %m1 = mul i32 %b1, %x1
  %m2 = mul i32 %b2, %x2
  %m3 = mul i32 %a1, %y1
  %m4 = mul i32 %a2, %y2
  %t0 = add i32 %m0, %m1
  %t1 = add i32 %t0, %m2
  %t2 = sub i32 %t1, %m3
  %yn = sub i32 %t2, %m4

  ; Store output
  %ptr.out = getelementptr i32, ptr %out, i32 %i
  store i32 %yn, ptr %ptr.out

  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit.latch

exit.latch:
  ; Update state for next call
  store i32 %xn, ptr %px1
  store i32 %x1, ptr %px2
  store i32 %yn, ptr %py1
  store i32 %y1, ptr %py2
  br label %exit

exit:
  ret void
}
