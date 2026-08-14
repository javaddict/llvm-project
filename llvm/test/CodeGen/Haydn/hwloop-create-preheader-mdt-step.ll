; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s

; Role: semantic — createPreheaderForLoop must update MDT (and parent MLI).

; REGRESSION: createPreheaderForLoop must update MDT (and parent MLI).
;
; raw_corr* Class-C gap (naturedsp-kernel-gaps): SMS reports
; Schedule Found? 1 (II=3) on the MAC loops (single-stage → discarded), then
; post-RA HaydnHardwareLoops must still form set_hwloop. Those loops need a
; dedicated preheader (guarded entry: header has latch + non-preheader guard).
;
; Bug: createPreheaderForLoop inserted NewPH but left MDT stale, so
; MDT->getNode(NewPH) was null and extractIVBump's findImmediateDefOnDomChain*
; walk failed immediately ("Cannot determine IV step") even when step=-1 lived
; as a dominating ADDI32 in entry. Fix mirrors HexagonHardwareLoops:
; MDT->addNewBlock(NewPH, IDom); MDT->changeImmediateDominator(Header, NewPH);
; ParentLoop->addBasicBlockToLoop(NewPH, *MLI) when parent exists.
;
; Shape under test (dominant FIR count-down):
; guarded entry (runtime N may be 0);
; step reg = -1 materialized in entry;
; latch: SEQ32 iv, 0; BEQZ back-edge (Case 2: trip = IV init).
;
; Without MDT update this stays soft beqz; with the fix it is set_hwloop_f2.


define i32 @countdown_needs_preheader(ptr %p, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %pre, label %exit

pre:
  br label %loop

loop:
  %i = phi i32 [ %n, %pre ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %pre ], [ %acc.next, %loop ]
  %idx = sub i32 %n, %i
  %addr = getelementptr inbounds i32, ptr %p, i32 %idx
  %v = load i32, ptr %addr, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, -1
  %cmp = icmp ne i32 %i.next, 0
  br i1 %cmp, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

; CHECK-LABEL: countdown_nested_outer_guard:
; Outer soft + inner hwloop is OK; require at least one set_hwloop for the
; count-down body after a dedicated preheader is created.
; CHECK: set_hwloop_f2
define void @countdown_nested_outer_guard(ptr %p, i32 %n, i32 %m) nounwind {
entry:
  %cmpn = icmp sgt i32 %n, 0
  br i1 %cmpn, label %outer.pre, label %exit

outer.pre:
  br label %outer

outer:
  %oi = phi i32 [ 0, %outer.pre ], [ %oi.next, %outer.latch ]
  %cmpm = icmp sgt i32 %m, 0
  br i1 %cmpm, label %inner.pre, label %outer.latch

inner.pre:
  br label %inner

inner:
  %i = phi i32 [ %m, %inner.pre ], [ %i.next, %inner ]
  %addr = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %addr, align 4
  store i32 %v, ptr %addr, align 4
  %i.next = add i32 %i, -1
  %cmp = icmp ne i32 %i.next, 0
  br i1 %cmp, label %inner, label %outer.latch

outer.latch:
  %oi.next = add i32 %oi, 1
  %ocmp = icmp slt i32 %oi.next, %n
  br i1 %ocmp, label %outer, label %exit

exit:
  ret void
}
