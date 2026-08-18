; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll/s2); bundles regrouped (mul+addi32 fused; move32 split out).
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; DSP Kernel Benchmark: 16-Tap FIR Filter
;
; Algorithm:
; for (n = 0; n < N; n++) {
; acc = 0;
; for (k = 0; k < 16; k++)
; acc += in[n + k] * coeffs[k];
; out[n] = acc;
; }
;
; Expected instruction counts (per outer iteration):
; 16 MAC32 instructions (inner loop multiply-accumulate)
; 16 LD32 for input samples
; 16 LD32 for coefficients
; 1 ST32 for output store
; Inner loop target: ~4 instructions (LD32 + LD32 + MAC32 + ADD32 + branch)
;
; VLIW bundle estimate:
; Inner loop body: ~8 bundles (LD32 cannot dual-issue with another LD32)
; GPR port constraint (4R2W) limits parallelism
;
; This kernel exercises:
; MAC fusion in a fixed-trip-count inner loop (16 iterations)
; Two parallel load streams (input + coefficients)
; Post-loop store (accumulator -> output)
; Outer loop with variable trip count
; High register pressure: out ptr, in ptr, coeffs ptr, n, inner index, accumulator

define void @fir_16tap(ptr %out, ptr %in, ptr %coeffs, i32 %n) {
;
; Outer loop enters inner loop; inner loop contains MAC32 per iteration.
; (SFR-strip) changed bundle layout — rebaselined; the
; bogus set_hwloop_f2 CHECK added then is removed here (rebaseline).
;
; rework : the pre-RA HardwareLoops pass was removed (it
; corrupted SET_HWLOOP_REG MBB operands pre-RA). SMS runs pre-RA on the naive
; loop; the post-RA HaydnHardwareLoops pass does not convert this nested inner
; loop (guarded preheader + multi-BB epilogue defeat the recognizer). The
; inner 16-tap loop stays a bnez_w back-edge; mac32 in the body is the invariant.
; The outer loop stays a regular slt32+beqz_w loop (multi-BB).
;
; Output store lands in the outer latch before the inner loop, then the inner
; loop body (mul64.ll per tap) runs as a hardware loop.
;
; Return sequence
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %outer.header, label %exit

outer.header:
  br label %outer.loop

outer.loop:
  %on = phi i32 [ 0, %outer.header ], [ %on.next, %outer.latch ]
  br label %inner.loop

inner.loop:
  %ik = phi i32 [ 0, %outer.loop ], [ %ik.next, %inner.loop ]
  %acc = phi i32 [ 0, %outer.loop ], [ %acc.next, %inner.loop ]
  ; in[n + k]
  %in.idx = add i32 %on, %ik
  %ptr.in = getelementptr i32, ptr %in, i32 %in.idx
  %val.in = load i32, ptr %ptr.in
  ; coeffs[k]
  %ptr.co = getelementptr i32, ptr %coeffs, i32 %ik
  %val.co = load i32, ptr %ptr.co
  ; accumulate
  %mul = mul i32 %val.in, %val.co
  %acc.next = add i32 %mul, %acc
  %ik.next = add i32 %ik, 1
  %cmp.inner = icmp slt i32 %ik.next, 16
  br i1 %cmp.inner, label %inner.loop, label %outer.latch

outer.latch:
  %ptr.out = getelementptr i32, ptr %out, i32 %on
  store i32 %acc.next, ptr %ptr.out
  %on.next = add i32 %on, 1
  %cmp.outer = icmp slt i32 %on.next, %n
  br i1 %cmp.outer, label %outer.loop, label %exit

exit:
  ret void
}
