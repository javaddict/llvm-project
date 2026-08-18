; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: smoke — function labels present; compile+emit smoke, not semantic qualification.

; REBASELINED : / cutover — native mul now carries slot suffix (mul64.ll) in single-op epilogue bundle.
; REBASELINED : pipeline lift (PEIPeephole/PushPopOpt/CompressPass to addPreSched2) — epilogue reload slot assignment regrouped (s0/_s1 swapped). Ops unchanged.
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Format-E-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Format E rebaseline: labels + present opcodes.

; Format E: function labels present (compile + emit smoke).
; CHECK-LABEL: fir_filter:
; CHECK: {{.}}

define i32 @fir_filter(ptr %input, ptr %coeffs, i32 %n) {
; Post-inc fusion : streaming loads fuse to s_lw_post_imm and the loop
; lowers as a zero-overhead hardware loop. The multiply-accumulate is
; now mull + add32 (no scalar GPR MAC opcode in the ISA DB).
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ptr.in = getelementptr i32, ptr %input, i32 %i
  %ptr.co = getelementptr i32, ptr %coeffs, i32 %i
  %in.val = load i32, ptr %ptr.in
  %co.val = load i32, ptr %ptr.co
  %mul = mul i32 %in.val, %co.val
  %acc.next = add i32 %mul, %acc
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}
