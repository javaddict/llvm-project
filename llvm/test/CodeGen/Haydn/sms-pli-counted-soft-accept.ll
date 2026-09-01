; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner \
; RUN:     < %s -o %t.s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; REQUIRES: asserts

; Role: peer-law PLI residual — proven counted soft branch is eligible
; (AIE DownCountLoop peer). Decrementing IV with trip = init is proven;
; StageCount==1 may accept as bare logical MIs (metrics-only hooks).
; StageCount>1 remains rejected. Non-zero-init incrementing reject is
; sms-pli-nonzero-init-reject.ll.

; SWP: SMS-HANDOFF: coverage ok
; SWP-DAG: Schedule Found? 1
; SWP-NOT: Unable to analyzeLoop
; SWP-NOT: SMS: reject incrementing IV with non-zero init
; If StageCount==1: accept metrics-only; if multi-stage found: containment.
; SWP-NOT: SMS-SHOULDUSE: accept multi-stage durable
; SWP-NOT: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}

; ASM-LABEL: count_down_sum:
; ASM-NOT: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM: jalr

define i32 @count_down_sum(i32 %n) {
entry:
  %c0 = icmp eq i32 %n, 0
  br i1 %c0, label %exit, label %loop
loop:
  %i = phi i32 [ %n, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, -1
  %cond = icmp ne i32 %i.next, 0
  br i1 %cond, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  ret i32 %r
}
