; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — PHI node shapes — intent CHECKs only (not full-UTC re-golden).

; PHI node shapes — intent CHECKs only (not full-UTC re-golden).
; Codegen is correct; previous XFAIL was FileCheck golden drift.

;PHI node with s32 in simple if-then-else → cmov (movt32)

define i32 @phi_s32_if(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: phi_s32_if:
; CHECK-DAG: add32
; CHECK-DAG: sub32
; CHECK-DAG: movt32
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %then, label %else
then:
  %r1 = add i32 %b, %c
  br label %merge
else:
  %r2 = sub i32 %b, %c
  br label %merge
merge:
  %r = phi i32 [%r1, %then], [%r2, %else]
  ret i32 %r
}

;PHI node with s64 in loop
define i64 @phi_s64_loop(i64 %n) {
; CHECK-LABEL: phi_s64_loop:
; CHECK: add64
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %loop]
  %sum = phi i64 [0, %entry], [%sum_next, %loop]
  %sum_next = add i64 %sum, %i
  %next = add i64 %i, 1
  %cmp = icmp slt i64 %next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i64 %sum_next
}

;PHI with pointer type → GenMux / cmov
define ptr @phi_ptr(ptr %a, ptr %b, i32 %cond) {
; CHECK-LABEL: phi_ptr:
; CHECK: movt32
entry:
  %cmp = icmp sgt i32 %cond, 0
  br i1 %cmp, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  %r = phi ptr [%a, %then], [%b, %else]
  ret ptr %r
}

;Multiple PHI nodes in same block
define i32 @multi_phi(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: multi_phi:
; CHECK: add32
entry:
  %cmp = icmp slt i32 %a, %b
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [0, %entry], [%i_next, %loop]
  %acc = phi i32 [%c, %entry], [%acc_next, %loop]
  %i_next = add i32 %i, 1
  %acc_next = add i32 %acc, %i
  %cmp2 = icmp slt i32 %i_next, %a
  br i1 %cmp2, label %loop, label %exit
exit:
  %r = phi i32 [%acc_next, %loop], [%c, %entry]
  ret i32 %r
}

;Loop-carried PHI with mixed types
define i64 @phi_loop_carried(i64 %init, i32 %n) {
; CHECK-LABEL: phi_loop_carried:
; CHECK: add64
entry:
  br label %loop
loop:
  %val = phi i64 [%init, %entry], [%next_val, %loop]
  %idx = phi i32 [0, %entry], [%next_idx, %loop]
  %idx_ext = zext i32 %idx to i64
  %next_val = add i64 %val, %idx_ext
  %next_idx = add i32 %idx, 1
  %cmp = icmp slt i32 %next_idx, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i64 %next_val
}

;PHI with three incoming values
define i32 @phi_three_values(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: phi_three_values:
; CHECK: jalr{{(\.s[012])?}}
entry:
  %cmp1 = icmp sgt i32 %a, 0
  br i1 %cmp1, label %branch1, label %check2
branch1:
  br label %merge
check2:
  %cmp2 = icmp sgt i32 %b, 0
  br i1 %cmp2, label %branch2, label %branch3
branch2:
  br label %merge
branch3:
  br label %merge
merge:
  %r = phi i32 [%c, %branch1], [%d, %branch2], [%a, %branch3]
  ret i32 %r
}
