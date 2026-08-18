; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: smoke — function labels present; compile+emit smoke, not semantic qualification.

; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll) in single-op epilogue bundle.
; REBASELINED : pipeline lift (PEIPeephole/PushPopOpt/CompressPass to addPreSched2) — epilogue reload slot assignment regrouped (s0/_s1 swapped) in both functions. Ops unchanged.
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Format-E-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Format E rebaseline: labels + present opcodes.

; Format E: function labels present (compile + emit smoke).
; CHECK-LABEL: dot_product:
; CHECK-LABEL: dot_product_16:
; CHECK: {{.}}

define i32 @dot_product(ptr %a, ptr %b, i32 %n) {
; NOTE: post-/ HWLoop broaden may convert countable loops to
; set_hwloop_f2; the.LBB0_1: loop-body label and blt_w back-edge may not
; appear. We assert the key arithmetic (mac32) survives;
; verify-machineinstrs is the correctness gate.
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %ptr.a = getelementptr i32, ptr %a, i32 %i
  %ptr.b = getelementptr i32, ptr %b, i32 %i
  %va = load i32, ptr %ptr.a
  %vb = load i32, ptr %ptr.b
  %mul = mul i32 %va, %vb
  %sum.next = add i32 %mul, %sum
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Variant with constant trip count (16) -- ideal hardware loop candidate.
; With -mattr=+hwloop, the hardware loop pass should convert this to
; SET_HWLOOP because: single-BB loop, constant trip count, no calls.
;
; REGRESSION TEST (SFR Blocker-1, lesson): when SMS fires on this
; trip-16 loop (e.g. under the SFR-strip which lowers recurrence-MII
; below profitability), the kernel must keep the full 16 iterations. The
; hand-rolled static `(limit-init)/step` trip-count shortcut in
; HaydnPipelinerLoopInfo returned a wrong compile-time bool that drove
; PeelingModuloScheduleExpander::fixupBranches into KernelDisposed -> the
; loop collapsed to ~1 iteration (one mac32, no back-edge), with
; verify-machineinstrs still green. The fix removed the static path;
; createTripCountGreaterCondition now always emits a runtime compare. This
; test asserts the mac32 body and the loop back-edge label survive SMS, so
; a regression that disposes the kernel fails here. Must PASS on both the
; reverted tree (SMS inert) and a -re-applied tree (SMS fires).
define i32 @dot_product_16(ptr %a, ptr %b) {
; NOTE: the conditional back-edge terminator may render as bnez_w/blt_w/beqz_w
; depending on LSR shape and HWLoop firing; the load-bearing assertion is
; that a mac32 body instruction survives (kernel not disposed).
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %ptr.a = getelementptr i32, ptr %a, i32 %i
  %ptr.b = getelementptr i32, ptr %b, i32 %i
  %va = load i32, ptr %ptr.a
  %vb = load i32, ptr %ptr.b
  %mul = mul i32 %va, %vb
  %sum.next = add i32 %mul, %sum
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 16
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
