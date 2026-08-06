; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s

; Role: semantic — PR6 / Phase C2b: nested loops — outer software / JNZD style, inner ZOL.

; PR6 / Phase C2b: nested loops — outer software / JNZD style, inner ZOL.
;
; TTI (HaydnTargetTransformInfo::isHardwareLoopProfitable):
; * innermost single-BB → CounterInReg=false (ZOL / LoopStart+PseudoLoopEnd)
; * outer (non-innermost) → CounterInReg=true, IsNestingLegal=true (JNZD
; LoopDec+LoopJNZ) when the outer is itself single-BB and profitable.
;
; Outer nests are multi-BB (contain the child), so IR HardwareLoops only
; forms the INNERMOST ZOL (CounterInReg=false). Outer may stay software
; cmp+branch, or post-RA may convert it under dual HWLR free-list
; finds a free sel and structure checks pass.
;
; Style reference: hwloop-nested-fir-16tap.ll.

define i32 @nested_outer_jnzd(ptr noalias %a, i32 %n, i32 %m) {
; CHECK-LABEL: nested_outer_jnzd:
; At least the inner ZOL forms.
; CHECK: set_hwloop
; CHECK: jalr
entry:
  %cmp.n = icmp sgt i32 %n, 0
  br i1 %cmp.n, label %outer.preheader, label %exit

outer.preheader:
  %cmp.m = icmp sgt i32 %m, 0
  br label %outer.header

outer.header:
  %i = phi i32 [ 0, %outer.preheader ], [ %i.next, %outer.latch ]
  %s = phi i32 [ 0, %outer.preheader ], [ %s.inner, %outer.latch ]
  br i1 %cmp.m, label %inner.preheader, label %outer.latch

inner.preheader:
  br label %inner.body

inner.body:
  %j = phi i32 [ 0, %inner.preheader ], [ %j.next, %inner.body ]
  %si = phi i32 [ %s, %inner.preheader ], [ %si.acc, %inner.body ]
  %idx = add i32 %i, %j
  %p = getelementptr inbounds i32, ptr %a, i32 %idx
  %v = load i32, ptr %p, align 4
  %si.acc = add i32 %si, %v
  %j.next = add i32 %j, 1
  %c.j = icmp eq i32 %j.next, %m
  br i1 %c.j, label %outer.latch, label %inner.body

outer.latch:
  %s.inner = phi i32 [ %s, %outer.header ], [ %si.acc, %inner.body ]
  %i.next = add i32 %i, 1
  %c.i = icmp eq i32 %i.next, %n
  br i1 %c.i, label %exit, label %outer.header

exit:
  %r = phi i32 [ 0, %entry ], [ %s.inner, %outer.latch ]
  ret i32 %r
}
