; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s
;
; Geometry (BundleSim code_image + AIE LoopSetupDistance peer):
;   - Inclusive END: BEGIN <= END is legal (short body OK)
;   - Hard rule: SET at least 3 Bundle128 parcels before BEGIN (t−3)
;
; A short single-BB countable loop must emit set_hwloop with a setup gap of
; at least three size-bearing parcels before the body start label.

define i32 @short_hwloop(i32 %n, ptr %p) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, 1
  %cont = icmp slt i32 %i.next, %n
  br i1 %cont, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

; CHECK: set_hwloop{{(_f2)?}}{{(_w)?}}
; Three size-bearing parcels after SET before body start (t−3).
; CHECK-NEXT: {
; CHECK-NEXT: {
; CHECK-NEXT: {
; CHECK: LLhwloop_start
; Short body is legal when t−3 holds (no forced min-body spray as product law).
; CHECK: LLhwloop_end
