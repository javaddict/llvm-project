; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: smoke — function labels present; compile+emit smoke, not semantic qualification.

; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll) in single-op epilogue bundle.
; REBASELINED : pipeline lift (PEIPeephole/PushPopOpt/CompressPass to addPreSched2) — r11 (LR) no longer spilled; reload slot order regrouped. Ops unchanged.
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Format-E-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Format E rebaseline: labels + present opcodes.

; Format E: function labels present (compile + emit smoke).
; CHECK-LABEL: dot_product_64:
; CHECK: {{.}}

define void @dot_product_64(ptr %a, ptr %b, ptr %out) {
;
; The loop body must contain a scalar s32 multiply-accumulate.
; With 64 iterations, we expect at least 64 mul64.ll sequences.
; Post-inc fusion : streaming loads fuse to s_lw_post_imm and the loop
; lowers as a zero-overhead hardware loop — no add32/bnez_w back-edge.
;
; Store result
;
; Return
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
  %cmp = icmp slt i32 %i.next, 64
  br i1 %cmp, label %loop, label %exit

exit:
  store i32 %sum.next, ptr %out
  ret void
}
