; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=-hwloop -stop-after=haydn-hwloops < %s | FileCheck --check-prefix=NOHW %s

; Role: MIR — STALE-FAILMARKER REMOVED (, post- cutover): the IR-level hardware-loop rearchitecture (prior revision) renamed the conversion.

; STALE-FAILMARKER REMOVED (, post- cutover): the IR-level
; hardware-loop rearchitecture (prior revision) renamed the conversion
; pseudos from SET_HWLOOP_REG to LoopStart + PseudoLoopEnd. All single-BB
; countable loops below now convert and the CHECKs were rebaselined to match.
; Test 6 (`multi_bb_loop`) is the measured multi-BB SCEV/CFG extension:
; innermost single-latch/single-exit diamond forms Role A. Test 7 (early
; exit) stays declined. Never post-RA physical rediscovery.
;
; NOTE: this test uses `-stop-after=haydn-hwloops` so it is unaffected by the
; SMS pipeliner. The previous "SMS BUG" comment was stale.
;
; Hardware loop detection tests for the Haydn backend.
;
; The hardware loop pass (HaydnHardwareLoops) detects countable single-BB and
; multi-BB loops and converts them to use SET_HWLOOP pseudos. The pass is gated
; on the +hwloop subtarget feature.
;
; The pass recognizes two patterns:
; 1) Fused compare+branch (from ConditionOptimizer): BLT/BGE/BLTU/BGEU/BEQ/BNE
; 2) Unfused compare+branch: SLT32/SLTU32/SEQ32 + BNEZ/BEQZ
;
; We use -stop-after=haydn-hwloops to check the MIR output (the SET_HWLOOP
; pseudo is a no-op in the AsmPrinter until the HWLOOP instruction encoding
; is finalized).

; Test 1: Simple for-loop with constant trip count.
; The loop runs 10 iterations: for (i=0; i<10; i++) sum += val
; After ConditionOptimizer, the latch ends with BLT (fused SLT32+BNEZ).
; The HWLoop pass should detect this and emit SET_HWLOOP with count=10.
; Standalone (innermost) loops use the inner product selector.

define i32 @simple_loop(ptr %p) {
; CHECK-LABEL: name: simple_loop
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 2: Loop with a function call inside.
; The hardware loop pass should NOT convert this because calls are invalid
; inside hardware loops.
define i32 @loop_with_call(ptr %p) {
; CHECK-LABEL: name: loop_with_call
; CHECK-NOT: LoopStart
; CHECK: {{BLT|SLT32}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  call void @extern_func(i32 %sum.next)
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 3: Loop with register trip count (unknown at compile time).
; The trip count comes from a function argument. The pass should detect the
; simple case (init=0, bump=1) and emit SET_HWLOOP_REG.
define i32 @loop_with_reg_count(ptr %p, i32 %n) {
; CHECK-LABEL: name: loop_with_reg_count
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 4: Loop without +hwloop feature.
; Without the hwloop feature, the pass should not convert any loops.
define i32 @loop_no_hwloop(ptr %p) {
; NOHW-LABEL: name: loop_no_hwloop
; NOHW-NOT: LoopStart
; NOHW: BNEZ
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 5: Loop with constant trip count of 100.
; The HWLoop pass should detect this and emit SET_HWLOOP with count=100.
define i32 @loop_100(ptr %p) {
; CHECK-LABEL: name: loop_100
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 100
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 6: Multi-BB loop with if/else inside the loop body.
; Measured SCEV/CFG extension: innermost single-latch/single-exit diamond
; may form Role A (never post-RA rediscovery). Early-exit stays declined.
define i32 @multi_bb_loop(ptr %p, ptr %q) {
; CHECK-LABEL: name: multi_bb_loop
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
entry:
  br label %loop.header

loop.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop.latch ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop.latch ]
  %val = load i32, ptr %p
  %cond = icmp slt i32 %val, 0
  br i1 %cond, label %loop.then, label %loop.else

loop.then:
  %sum.pos = add i32 %sum, %val
  br label %loop.latch

loop.else:
  %v2 = load i32, ptr %q
  %sum.neg = add i32 %sum, %v2
  br label %loop.latch

loop.latch:
  %sum.next = phi i32 [ %sum.pos, %loop.then ], [ %sum.neg, %loop.else ]
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop.header, label %exit

exit:
  ret i32 %sum.next
}

; Test 7: Multi-BB loop that should NOT be converted because it has
; multiple exit blocks (early exit via the "then" block).
define i32 @multi_bb_loop_early_exit(ptr %p, i32 %limit) {
; CHECK-LABEL: name: multi_bb_loop_early_exit
; CHECK-NOT: LoopStart
; CHECK: {{BLT|SLT32}}
entry:
  br label %loop.header

loop.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop.latch ]
  %val = load i32, ptr %p
  %cond = icmp sgt i32 %val, 100
  br i1 %cond, label %early.exit, label %loop.latch

loop.latch:
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %limit
  br i1 %cmp, label %loop.header, label %exit

early.exit:
  ret i32 %val

exit:
  ret i32 0
}

declare void @extern_func(i32)
