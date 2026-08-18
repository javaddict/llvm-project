; RUN: opt -passes=hardware-loops -mtriple=haydn-unknown-elf \
; RUN:   -S < %s 2>&1 | FileCheck %s --check-prefix=IR
; REQUIRES: haydn-registered-target

; Role: semantic — Phase 1: verify the upstream IR-level HardwareLoops pass fires on Haydn.


;
; Phase 1: verify the upstream IR-level HardwareLoops pass fires on Haydn.
; The TTI hook (HaydnTargetTransformInfo::isHardwareLoopProfitable) accepts
; innermost single-latch/single-exit loops (single-BB, or a measured multi-BB
; diamond) with a loop-invariant backedge-taken count. SCEV derives the
; trip count symbolically. The pass inserts llvm.set.loop.iterations in the
; preheader and llvm.loop.decrement in the latch.
;
; This test only checks the IR dump: the RUN line is opt
; -passes=hardware-loops and never invokes llc, so no global-isel flags
; apply. The IR-level pass firing is the Phase 1 deliverable.
;
; This is a simple count-up loop with a runtime trip count (N). SCEV resolves
; it to {N,+,1} and the pass should insert the intrinsics.

define void @ir_level_hwloop(ptr nocapture %p, i32 %n) {
; IR-LABEL: ir_level_hwloop
; IR: call void @llvm.set.loop.iterations
; IR: call i1 @llvm.loop.decrement
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %pp = phi ptr [ %p, %entry ], [ %pp.next, %loop ]
  store i32 %i, ptr %pp, align 4
  %pp.next = getelementptr inbounds i32, ptr %pp, i32 1
  %i.next = add nuw i32 %i, 1
  %cmp = icmp ult i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Innermost latch-only diamond: measured multi-BB SCEV/CFG extension.
define i32 @ir_level_multibb_diamond(ptr nocapture readonly %p, ptr nocapture readonly %q, i32 %n) {
; IR-LABEL: ir_level_multibb_diamond
; IR: call void @llvm.set.loop.iterations
; IR: call i1 @llvm.loop.decrement
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %latch ]
  %v = load i32, ptr %p, align 4
  %neg = icmp slt i32 %v, 0
  br i1 %neg, label %then, label %else

then:
  %sp = add i32 %s, %v
  br label %latch

else:
  %vq = load i32, ptr %q, align 4
  %sn = add i32 %s, %vq
  br label %latch

latch:
  %s.next = phi i32 [ %sp, %then ], [ %sn, %else ]
  %i.next = add nuw i32 %i, 1
  %c = icmp ult i32 %i.next, %n
  br i1 %c, label %header, label %exit

exit:
  ret i32 %s.next
}

; Early-exit multi-BB stays declined (latch is not the unique exiting block).
define i32 @ir_level_multibb_early_exit(ptr nocapture readonly %p, i32 %n, i32 %k) {
; IR-LABEL: ir_level_multibb_early_exit
; IR-NOT: call void @llvm.set.loop.iterations
; IR-NOT: call i1 @llvm.loop.decrement
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %v = load i32, ptr %p, align 4
  %hit = icmp eq i32 %v, %k
  br i1 %hit, label %early, label %latch

latch:
  %i.next = add nuw i32 %i, 1
  %c = icmp ult i32 %i.next, %n
  br i1 %c, label %header, label %exit

early:
  ret i32 %v

exit:
  ret i32 0
}
