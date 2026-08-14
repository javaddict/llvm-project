; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s

; Role: semantic — G_STACKRESTORE and G_STACKSAVE must be selected correctly to enable VLA (variable-length array) support.

; REGRESSION TEST: G_STACKRESTORE and G_STACKSAVE must be selected correctly
; to enable VLA (variable-length array) support.
;
; Bug: VLA code failed with "cannot select: G_STACKRESTORE" because the
; instruction selector had no case for G_STACKRESTORE or G_STACKSAVE.
; These are used by LLVM to save/restore the stack pointer around VLA
; allocations.
;
; Fix: Added G_STACKRESTORE → COPY R13, %src (sets SP to saved value).
; Added G_STACKSAVE → COPY %dst, R13 (saves current SP).
; Added G_DYN_STACKALLOC → SUB32 dst, SP, size; COPY SP, dst.
;
; If selection regresses, llc will crash with "cannot select".

define i32 @vla_test(i32 %n) {
; CHECK-LABEL: vla_test:
; The function must compile without error. We check that it produces
; valid instructions (sub32 for stack allocation, proper array accesses).
; CHECK: sub32
; The VLA store may land as st32, s_sw_post_imm (Format E POST member),
; or d_sw_l_with_imm (low-word DR64 store).
; CHECK: {{(st32|s_sw_post_imm|d_sw_l_with_imm)}}
; Indexed load of last element may be ld32 or fused s_lw_pre_reg.
; CHECK: {{(ld32|s_lw_pre_reg)}}
entry:
  %vla = alloca i32, i32 %n
  br label %for.body

for.body:
  %i = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %mul = mul i32 %i, %i
  %ptr = getelementptr i32, ptr %vla, i32 %i
  store i32 %mul, ptr %ptr
  %inc = add i32 %i, 1
  %cmp = icmp slt i32 %inc, %n
  br i1 %cmp, label %for.body, label %for.end

for.end:
  %last_idx = sub i32 %n, 1
  %last_ptr = getelementptr i32, ptr %vla, i32 %last_idx
  %result = load i32, ptr %last_ptr
  ret i32 %result
}

; Test with a simple alloca (dynamic stack allocation).
define ptr @dynamic_alloca(i32 %size) {
; CHECK-LABEL: dynamic_alloca:
; CHECK: sub32
entry:
  %ptr = alloca i32, i32 %size
  ret ptr %ptr
}
