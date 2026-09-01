; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 -window-sched=force \
; RUN:     -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=PROD
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops=0 \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -haydn-zol-pipelining=0 -window-sched=force \
; RUN:     -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SOFT
; REQUIRES: asserts

; Role: D1.29 (2026-08-30) — pins the WindowScheduler forfeit as a
; deliberate three-layer fact (tracked as its own GOALS row), not an
; accident, on both flag arms:
;
;  PROD arm (product defaults + -window-sched=force): canPipelineLoop PASSES
;  ZOL loops (analyzeLoopForPipelining returns HaydnPipelinerLoopInfo), so
;  the generic driver WOULD run the WindowScheduler on them after SMS —
;  HaydnSubtarget::enableWindowScheduler() returning false under
;  EnableZOLPipelining is the SOLE blocker. Polarity: WS never initializes
;  ("Target disables the window scheduling!"), even under force.
;
;  SOFT arm (-haydn-zol-pipelining=0 -haydn-enable-hwloops=0): the override
;  now returns true, and WS still declines — independently — at initialize()
;  because the soft loop-control chain (EndLoop/CmpMI/InvertMI) is in the
;  Haydn ignore set ("Special MI defined by target is not allowed in window
;  scheduling!"). Unblocking WS for Haydn needs the HC#0-recorded common
;  delta (meta-terminator TripleMBB handling + a ZOL expand law) AND a
;  target-owned ignore-set law; flipping the override alone does nothing.

; Debug lines print before the asm, so the polarity pins come first.
; PROD: Target disables the window scheduling!
; PROD: The WindowScheduler failed to initialize!
; PROD-NOT: Special MI defined by target is not allowed in window scheduling!
; PROD-NOT: Window scheduling is not needed!
; PROD-LABEL: copy_const_64:
; PROD: set_hwloop_f2

; SOFT: Special MI defined by target is not allowed in window scheduling!
; SOFT: The WindowScheduler failed to initialize!
; SOFT-NOT: SMS-SHOULDUSE: accept
; SOFT-NOT: Target disables the window scheduling!

@src = global [128 x i8] zeroinitializer, align 4
@dst = global [128 x i8] zeroinitializer, align 4

define void @copy_const_64() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds [128 x i8], ptr @src, i32 0, i32 %i
  %v = load i8, ptr %sp, align 1
  %dp = getelementptr inbounds [128 x i8], ptr @dst, i32 0, i32 %i
  store i8 %v, ptr %dp, align 1
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.next, 64
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
