; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: semantic — product multi-stage SMS (always durable groups, no handoff
; switch). CoreMark matrix_sum-like residual.
;
; History: bare multi-stage expand (no durable groups) produced ORACLE_MISMATCH.
; Product path always accepts multi-stage and materializes clone→cycle BUNDLEs.
;
; Contract:
;   accept multi-stage durable + materialize groups>0
;   Assembly: SWPS multi-stage kernel annotation stages>=2

; SWP: Schedule Found? 1
; SWP: SMS-SHOULDUSE: accept multi-stage durable stages={{[2-9]|[1-9][0-9]+}}
; SWP: SMS-HANDOFF: materialize done groups={{[1-9][0-9]*}}
; SWP-NOT: SMS-SHOULDUSE: reject multi-stage naive
; SWP-NOT: Target rejected schedule

; ASM-LABEL: matrix_sum_like:
; ASM: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM: jalr_w

define i32 @matrix_sum_like(ptr nocapture readonly %C, i32 %N, i32 %clip) {
entry:
  %c0 = icmp eq i32 %N, 0
  br i1 %c0, label %exit, label %outer

outer:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %ret = phi i32 [ 0, %entry ], [ %ret.o, %outer.latch ]
  %prev = phi i32 [ 0, %entry ], [ %prev.o, %outer.latch ]
  %tmp0 = phi i32 [ 0, %entry ], [ %tmp.o, %outer.latch ]
  br label %inner

inner:
  %j = phi i32 [ 0, %outer ], [ %j.next, %inner ]
  %ret.i = phi i32 [ %ret, %outer ], [ %ret.next, %inner ]
  %prev.i = phi i32 [ %prev, %outer ], [ %cur, %inner ]
  %tmp.i = phi i32 [ %tmp0, %outer ], [ %tmp.next, %inner ]
  %idx = mul i32 %i, %N
  %idx2 = add i32 %idx, %j
  %p = getelementptr inbounds i32, ptr %C, i32 %idx2
  %cur = load i32, ptr %p, align 4
  %tmp.add = add i32 %tmp.i, %cur
  %gt = icmp sgt i32 %tmp.add, %clip
  %ret.a = add i32 %ret.i, 10
  %cmpcur = icmp sgt i32 %cur, %prev.i
  %one = zext i1 %cmpcur to i32
  %ret.b = add i32 %ret.i, %one
  %ret.next = select i1 %gt, i32 %ret.a, i32 %ret.b
  %tmp.next = select i1 %gt, i32 0, i32 %tmp.add
  %j.next = add nuw nsw i32 %j, 1
  %cond = icmp eq i32 %j.next, %N
  br i1 %cond, label %outer.latch, label %inner

outer.latch:
  %ret.o = phi i32 [ %ret.next, %inner ]
  %prev.o = phi i32 [ %cur, %inner ]
  %tmp.o = phi i32 [ %tmp.next, %inner ]
  %i.next = add nuw nsw i32 %i, 1
  %ocond = icmp eq i32 %i.next, %N
  br i1 %ocond, label %exit, label %outer

exit:
  %r = phi i32 [ 0, %entry ], [ %ret.o, %outer.latch ]
  ret i32 %r
}
