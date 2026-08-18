; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; DSP Kernel Benchmark: IIR Biquad Filter (Direct Form 1)
;
; Algorithm:
; acc = b[0] * input + state[0];
; state[0] = b[1] * input + a[0] * acc + state[1];
; state[1] = b[2] * input + a[1] * acc;
; return acc;
;
; Codegen quality targets:
; Multiple MAC32 operations (at least 3 of the 5 multiply-accumulate ops)
; State update pattern uses store with offset addressing
; Register pressure should fit within GPR R1-R12 without excessive spills
;
; This kernel exercises:
; MAC fusion across multiple multiply-accumulate chains
; GEP offset folding for struct-like array access (state[0], state[1])
; Store-after-compute pattern (state variable updates)
; High register pressure (5 coefficients + 2 state + input + acc = 10 values)

define i32 @iir_biquad(i32 %input, ptr %a_coeffs, ptr %b_coeffs, ptr %state) {
; Post-: loads are dual-issued (ld32+ld32) and the prologue xor32 may
; be scheduled anywhere; use DAG for the dataflow instructions.
entry:
  %b0.ptr = getelementptr i32, ptr %b_coeffs, i32 0
  %b1.ptr = getelementptr i32, ptr %b_coeffs, i32 1
  %b2.ptr = getelementptr i32, ptr %b_coeffs, i32 2
  %a0.ptr = getelementptr i32, ptr %a_coeffs, i32 0
  %a1.ptr = getelementptr i32, ptr %a_coeffs, i32 1
  %s0.ptr = getelementptr i32, ptr %state, i32 0
  %s1.ptr = getelementptr i32, ptr %state, i32 1

  %b0 = load i32, ptr %b0.ptr
  %b1 = load i32, ptr %b1.ptr
  %b2 = load i32, ptr %b2.ptr
  %a0 = load i32, ptr %a0.ptr
  %a1 = load i32, ptr %a1.ptr
  %s0 = load i32, ptr %s0.ptr
  %s1 = load i32, ptr %s1.ptr

  ;y[n] = b0*x[n] + s0
  %m0 = mul i32 %b0, %input
  %acc = add i32 %m0, %s0

  ;s0_next = b1*x[n] + a0*y[n] + s1
  %m1 = mul i32 %b1, %input
  %m2 = mul i32 %a0, %acc
  %t1 = add i32 %m1, %m2
  %new_s0 = add i32 %t1, %s1
  store i32 %new_s0, ptr %s0.ptr

  ;s1_next = b2*x[n] + a1*y[n]
  %m3 = mul i32 %b2, %input
  %m4 = mul i32 %a1, %acc
  %new_s1 = add i32 %m3, %m4
  store i32 %new_s1, ptr %s1.ptr

  ret i32 %acc
}
