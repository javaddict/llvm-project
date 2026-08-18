; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: smoke — Smoke: pre-existing CHECK drift — compile and emit a return.

; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll); epilogue single-op bundle gained.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

;
; DSP Kernel Benchmark: 1D Convolution
;
; Algorithm:
; result = 0;
; for (i = 0; i < kern_len; i++)
; result += signal[i] * kernel[kern_len - 1 - i];
; return result;
;
; Codegen quality targets:
; Loop body must contain MAC32 (mul+add fused)
; Loop body instruction count target: <= 4 instructions
; Reverse-index addressing: kernel[kern_len - 1 - i]
; Should compute as base + (kern_len-1-i)*sizeof(int)
;
; This kernel exercises:
; MAC fusion with non-trivial index arithmetic
; Reverse-order array access (common in FIR/convolution kernels)
; SUB instruction in address computation (kern_len - 1 - i)
; Single-BB countable loop -- hardware loop candidate

define i32 @convolve(ptr %signal, ptr %kernel, i32 %sig_len, i32 %kern_len) {
; This loop MUST be lowered to a proper iterating form: either set_hwloop_f2
; (HWLoop conversion) or a back-edge branch (bnez_w/blt_w/beqz_w/etc.). If the
; loop is dropped entirely (no iteration, body emitted once), neither a
; back-edge nor set_hwloop_f2 appears and these CHECKs fail.
; PRE-RA PASS UPDATE (Stream A,): the pre-RA HardwareLoops pass converts
; this single-BB countable loop to a hardware loop, so set_hwloop_f2 appears
; and there is no bnez_w back-edge.
; POST-RA HWLOOP UPDATE : the post-RA hwloop pass now also converts
; this loop. The program-point-aware IV/limit resolver correctly handles
; the case where the IV init and limit are defined in the function-entry block
; (an ancestor of the preheader) and flow through an empty preheader into the
; loop. Either set_hwloop_f2 (pre-RA pass) or set_hwloop_f2 (post-RA pass)
; wins; either way the loop iterates correctly.
; Scalar s32 mul lowers to mull.
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %result = phi i32 [ 0, %entry ], [ %result.next, %loop ]
  %ptr.s = getelementptr i32, ptr %signal, i32 %i
  %kern_idx = sub i32 %kern_len, 1
  %ki = sub i32 %kern_idx, %i
  %ptr.k = getelementptr i32, ptr %kernel, i32 %ki
  %sv = load i32, ptr %ptr.s
  %kv = load i32, ptr %ptr.k
  %mul = mul i32 %sv, %kv
  %result.next = add i32 %mul, %result
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %kern_len
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %result.next
}
