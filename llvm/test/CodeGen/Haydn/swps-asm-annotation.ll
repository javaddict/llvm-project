; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN: -verify-machineinstrs -O2 < %s | FileCheck %s

; Role: semantic — mac_loop scalar product uses MULL after G_MUL s32 rewire.

; mac_loop scalar product uses MULL after G_MUL s32 rewire.
; (SWPS annotation presence is schedule-dependent; covered by sw-pipeline.ll.)

define i32 @mac_loop(ptr nocapture readonly %x, ptr nocapture readonly %h, i32 %n) {
; CHECK-LABEL: mac_loop:
; CHECK: mull
; CHECK-NOT: mul64.ll
; CHECK: jalr
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %px = getelementptr inbounds i32, ptr %x, i32 %i
  %ph = getelementptr inbounds i32, ptr %h, i32 %i
  %xv = load i32, ptr %px, align 4
  %hv = load i32, ptr %ph, align 4
  %prod = mul i32 %xv, %hv
  %acc.next = add i32 %acc, %prod
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}
