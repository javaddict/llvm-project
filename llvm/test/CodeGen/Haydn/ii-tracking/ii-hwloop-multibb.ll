; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — product default OFF: a multi-BB diamond stays a software
; loop. Measured HWON Role-A expand is pinned by hwloop-multibb.ll.

; REGRESSION TEST: product `-haydn-enable-hwloops` is OFF. TTI may accept an
; innermost single-latch/single-exit diamond, but the expand pass is not in
; the product pipeline, so this shape must stay a compare-and-branch
; back-edge. Opening the product flag is a separate policy change.

define void @ii_hwloop_multibb(ptr %dst, ptr readonly %src, i32 %n) {
; CHECK-LABEL: ii_hwloop_multibb:
; CHECK-NOT:   set_hwloop
; Software back-edge (form may be fused blt_w or slt+bnez — either is fine).
; CHECK:       {{blt|bnez|beqz}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  br label %latch

else:
  br label %latch

latch:
  %out = phi i32 [ 0, %then ], [ %v, %else ]
  store i32 %out, ptr %dp
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
