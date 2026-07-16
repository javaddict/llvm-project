; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -mattr=+hwloop \
; RUN:   < %s | FileCheck %s
;
; Soft-div / rem must NOT form hardware loops (TTI H1 /).
;
; Haydn has no hardware divider: udiv/urem/sdiv/srem lower to libcalls
; (__udivsi3 / __umodsi3 /...). Those callees themselves may use hwloop
; (sel=1), so forming a caller hwloop around the jal would clobber HWLR1
; mid-iteration. HaydnTTI::isHardwareLoopProfitable therefore rejects any
; loop whose body contains integer/FP div or rem, even when the IR still
; has a clean countable single-BB shape.

define i32 @div_loop(i32 %n, i32 %d) {
; CHECK-LABEL: div_loop:
; CHECK-NOT: set_hwloop
; CHECK-NOT: loop_start
; Soft-div libcall (or residual software loop control) is expected.
; CHECK: jalr
entry:
  %cmp0 = icmp ugt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %q = udiv i32 %i, %d
  %acc.next = add i32 %acc, %q
  %i.next = add i32 %i, 1
  %cmp = icmp ult i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

define i32 @rem_loop(i32 %n, i32 %d) {
; CHECK-LABEL: rem_loop:
; CHECK-NOT: set_hwloop
; CHECK-NOT: loop_start
; CHECK: jalr
entry:
  %cmp0 = icmp ugt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %r = urem i32 %i, %d
  %acc.next = add i32 %acc, %r
  %i.next = add i32 %i, 1
  %cmp = icmp ult i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  %res = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %res
}
