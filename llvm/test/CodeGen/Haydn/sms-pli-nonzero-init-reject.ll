; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=haydn-instr-info,pipeliner \
; RUN:     < %s -o %t.s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; REQUIRES: asserts

; Role: Option A / peer-law PLI hygiene — proven trip count only.
; Incrementing IV with non-zero init (for (i = k; i < n; ++i)) must NOT
; treat LimitReg as the trip count. analyzeSimpleLoop rejects so
; multi-stage peel cannot mis-iterate. Soft-branch loop remains legal.
;
; SWP: SMS: reject incrementing IV with non-zero init
; SWP: Unable to analyzeLoop, can NOT pipeline Loop
; SWP-NOT: SMS-SHOULDUSE: accept
; SWP-NOT: SMS-HANDOFF: materialize

; ASM-LABEL: scan_from_k:
; ASM-NOT: #<swps>
; ASM: jalr

define i32 @scan_from_k(ptr nocapture readonly %p, i32 %k, i32 %n) {
entry:
  %c0 = icmp uge i32 %k, %n
  br i1 %c0, label %exit, label %loop
loop:
  %i = phi i32 [ %k, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %idx = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %idx, align 4
  %sum.next = add i32 %sum, %v
  %i.next = add nuw i32 %i, 1
  %cond = icmp ult i32 %i.next, %n
  br i1 %cond, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  ret i32 %r
}
