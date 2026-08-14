; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -O2 -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -O2 < %s | FileCheck %s

; Role: semantic — Software pipelining tests for the Haydn VLIW DSP backend.

; Software pipelining tests for the Haydn VLIW DSP backend.
;
; The Haydn backend integrates with LLVM's target-independent MachinePipeliner
; (Swing Modulo Scheduling). The pipeliner runs pre-RA at -O1+ and attempts to
; overlap loop iterations to maximize functional unit utilization on the 3-slot
; VLIW datapath.
;
; The target hook analyzeLoopForPipelining recognizes single-BB countable
; loops. IV detection runs at the MachinePipeliner, which executes
; PRE-PHIElimination : the loop block still contains its PHIs. The IV is
; a PHI whose latch-incoming value is the IV's own bump (ADD32/ADDI32/SUB32);
; findInductionVar recognizes both the compare-on-bump shape and the symmetric
; compare-on-PHI shape. The runtime trip-count register is the comparison's
; other (limit) operand. The trip-count comparison is ALWAYS emitted at runtime
; via createTripCountGreaterCondition (no static-TC shortcut -- see).
;
; STATUS (re-verified post- ResMII honesty + MAC acc lat):
; the G4 PHI-form recognizer (-rework,) ACCEPTS all 5 loops below.
; Schedule Found? prints before the MaxStageCount>0 gate — compact single
; stage finds are reported then discarded. Pin Found II at MII:
; simple_acc_loop -> Schedule Found? 1 (II={{[0-9]+}}) [ResMII=2]
; mac_loop -> Schedule Found? 1 (II=5) [MII=4]
; const_tc_loop -> Schedule Found? 1 (II={{[0-9]+}})
; runtime_tc_loop -> Schedule Found? 1 (II={{[0-9]+}})
; loop_with_call -> Unable to analyzeLoop [correct: call = unbreakable dep]
; loop_with_call asserts the rejection banner -- negative control.

; Test 1: Simple accumulation loop with constant trip count.
; The pipeliner should analyze this loop and attempt to schedule it.
; For this simple loop (3 instructions in the body), the pipeliner may find
; that there is no overlap benefit and leave the loop unchanged.

define i32 @simple_acc_loop(ptr nocapture readonly %p, i32 %n) {
; CHECK-LABEL: simple_acc_loop:
; CHECK:        // =>This Inner Loop Header: Depth=1
; Loop-carried add32 survives. Compare may be SMS-rewritten slt32/bnez_w or
; the original blt_w when the Found schedule is single-stage (discarded).
; CHECK:        add32
; CHECK-DAG:    {{(slt32|blt)}}
; SMS finds a schedule at MII (may be single-stage / discarded post-gate).
; SWP:          Schedule Found? 1 (II={{[0-9]+}})
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 2: MAC (multiply-accumulate) loop -- the most common DSP kernel pattern.
; The pipeliner should recognize this loop with 4 body instructions.
define i32 @mac_loop(ptr nocapture readonly %x, ptr nocapture readonly %h, i32 %n) {
; CHECK-LABEL: mac_loop:
; DR64 multiply (mull post- s32-via-MAC-unit lowering) in the kernel.
; Multi-stage SMS may emit a second epilogue mul64; single-stage keeps one.
; CHECK:        // =>This Inner Loop Header: Depth=1
; CHECK:        mull
; CHECK-DAG:    {{(slt32|blt|bnez)}}
; SMS finds a schedule for this MAC (mul-acc) kernel.
; SWP:          Schedule Found? 1 (II={{[0-9]+}})
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %xi = getelementptr i32, ptr %x, i32 %i
  %hi = getelementptr i32, ptr %h, i32 %i
  %xv = load i32, ptr %xi
  %hv = load i32, ptr %hi
  %mul = mul i32 %xv, %hv
  %acc.next = add i32 %acc, %mul
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}

; Test 3: Loop with constant trip count (10).
;
; REGRESSION TEST (SFR Blocker-1, lesson): the SMS
; createTripCountGreaterCondition hook must NEVER return a compile-time
; static bool for this loop. The hand-rolled `(limit-init)/step` shortcut
; returned std::optional<bool>(false) when its arithmetic mis-derived the
; trip count, which drove PeelingModuloScheduleExpander::fixupBranches
; (llvm/lib/CodeGen/ModuloSchedule.cpp:1980-1999) into the static-false
; branch -> KernelDisposed=true -> the kernel was erased and the loop ran
; ~1 iteration instead of 10. -verify-machineinstrs stayed GREEN throughout
; (silent wrong-code). The fix removes the static path entirely and always
; emits a runtime compare (mirrors ARM). This test asserts the loop kernel
; survives SMS: the inner-loop header label and a body instruction (add32)
; must still be present. If the static-TC regression returns, the kernel is
; disposed and both CHECKs fail.
;
; This test must PASS on both the -reverted tree (SMS inert -- normal
; loop) and a -re-applied tree (SMS may fire -- must still keep the
; full trip count).
define i32 @const_tc_loop(ptr nocapture readonly %p) {
; CHECK-LABEL: const_tc_loop:
; CHECK:        // =>This Inner Loop Header: Depth=1
; Kernel survives SMS (add32 + runtime compare vs limit 10). gate: the
; static-TC shortcut that disposed the kernel must NOT return — blt_w/slt32
; against the constant are both runtime compares.
; CHECK:        add32
; CHECK-DAG:    {{(slt32|blt)}}
; SMS finds a schedule (II=MII=2) while still emitting the runtime
; trip-count compare (the static-TC regression must NOT return).
; SWP:          Schedule Found? 1 (II={{[0-9]+}})
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 4: Loop with runtime trip count from function argument.
; The trip count is unknown at compile time, so createTripCountGreaterCondition
; must emit a dynamic comparison.
define i32 @runtime_tc_loop(ptr nocapture readonly %p, i32 %n) {
; CHECK-LABEL: runtime_tc_loop:
; CHECK:        // =>This Inner Loop Header: Depth=1
; add32 survives; runtime compare against %n (blt_w or SMS slt32/bnez_w).
; CHECK:        add32
; CHECK-DAG:    {{(slt32|blt)}}
; SMS finds a schedule for the runtime-trip-count loop.
; SWP:          Schedule Found? 1 (II={{[0-9]+}})
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 5: Loop containing a function call.
; REGRESSION NOTE (follow-up): the recognizer REJECTS this loop at the
; analyzeLoopForPipelining stage because the loop body contains a call
; (analyzeSimpleLoop's isCall/hasUnmodeledSideEffects guard, mirroring AIE's
; hasLockInstruction bail). This is stricter and cheaper than the prior
; behavior (analyze-accept + sms-profitability-reject): a loop with a call is a
; poor pipelining candidate regardless of II, so rejecting it early avoids
; feeding SMS a loop it can only discard. This is the NEGATIVE CONTROL for SMS
; recognition: the loop must NOT be pipelined.
define i32 @loop_with_call(ptr nocapture readonly %p) {
; CHECK-LABEL: loop_with_call:
; SWP:          Unable to analyzeLoop, can NOT pipeline Loop
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %pi = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %pi
  %sum.next = add i32 %sum, %val
  call void @extern_func(i32 %sum.next)
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

declare void @extern_func(i32)
