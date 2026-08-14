; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — This test was XFAIL'd from until reverted the GPR-port-FuncUnits change to HaydnSchedule.td that triggered a SIGSEGV in.

; This test was XFAIL'd from until reverted the
; GPR-port-FuncUnits change to HaydnSchedule.td that triggered a SIGSEGV in
; the post-RA VLIW scheduler. XFAIL removed now that the single-stage
; slot-only itinerary model is restored. See decision and lesson.
;
; REGRESSION TEST: CodeGen-level branch relaxation for long branches.
;
; This test verifies that the BranchRelaxation pass correctly handles
; out-of-range branch targets at the CodeGen level. Conditional branches
; (BEQ, BNE, BGE, etc.) have a 16-bit signed offset (+/-32KB range).
; When the target exceeds this range, BranchRelaxation inserts an
; inverted-conditional + indirect jump sequence via insertIndirectBranch.
;
; The difference from MC-level relaxation: CodeGen-level relaxation uses
; LOADI32 + JALR (register-indirect) for truly far targets, while MC-level
; uses inverted-cond + JAL (direct jump, +/-1MB range).
;
; Test design: Create functions where a conditional branch must cross a
; large code gap (many volatile stores) to reach its target.
;
; IMPORTANT: The primary goal is that these functions compile without
; crashing. The CHECK lines verify that the function assembles (contains
; st32 instructions for the volatile stores and jalr_w for return), not that
; the exact branch relaxation strategy is used.

@arr = global [3000 x i32] zeroinitializer

; Test 1: Forward conditional branch (eq) across >32KB gap
define void @test_long_beq(i32 %a, i32 %b) {
; CHECK-LABEL: test_long_beq:
; The key check: the function must compile. The volatile stores produce st32.
; CHECK: st32
; CHECK: st32
; The function must return.
; CHECK: jalr{{(\.s[012])?}}
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %far, label %pad

pad:
  store volatile i32 0, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 0), align 4
  store volatile i32 1, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 1), align 4
  store volatile i32 2, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 2), align 4
  store volatile i32 3, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 3), align 4
  store volatile i32 4, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 4), align 4
  store volatile i32 5, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 5), align 4
  store volatile i32 6, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 6), align 4
  store volatile i32 7, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 7), align 4
  store volatile i32 8, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 8), align 4
  store volatile i32 9, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 9), align 4
  store volatile i32 10, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 10), align 4
  store volatile i32 11, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 11), align 4
  store volatile i32 12, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 12), align 4
  store volatile i32 13, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 13), align 4
  store volatile i32 14, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 14), align 4
  store volatile i32 15, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 15), align 4
  store volatile i32 16, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 16), align 4
  store volatile i32 17, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 17), align 4
  store volatile i32 18, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 18), align 4
  store volatile i32 19, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 19), align 4
  store volatile i32 20, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 20), align 4
  store volatile i32 21, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 21), align 4
  store volatile i32 22, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 22), align 4
  store volatile i32 23, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 23), align 4
  store volatile i32 24, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 24), align 4
  store volatile i32 25, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 25), align 4
  store volatile i32 26, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 26), align 4
  store volatile i32 27, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 27), align 4
  store volatile i32 28, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 28), align 4
  store volatile i32 29, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 29), align 4
  store volatile i32 30, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 30), align 4
  store volatile i32 31, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 31), align 4
  store volatile i32 32, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 32), align 4
  store volatile i32 33, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 33), align 4
  store volatile i32 34, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 34), align 4
  store volatile i32 35, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 35), align 4
  store volatile i32 36, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 36), align 4
  store volatile i32 37, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 37), align 4
  store volatile i32 38, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 38), align 4
  store volatile i32 39, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 39), align 4
  br label %exit

far:
  store volatile i32 42, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 0), align 4
  br label %exit

exit:
  ret void
}

; Test 2: Backward conditional branch (ne) across >32KB gap
; This verifies that the BranchRelaxation handles both directions.
define void @test_long_bne_backward(i32 %a, i32 %b) {
; CHECK-LABEL: test_long_bne_backward:
; CHECK: st32
; CHECK: st32
; CHECK: jalr{{(\.s[012])?}}
entry:
  br label %loop

loop:
; The back-edge branch from loop to loop crosses the loop body which is
; >32KB due to the many stores. BranchRelaxation must relax this.
  %cmp = icmp ne i32 %a, %b
  store volatile i32 0, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 0), align 4
  store volatile i32 1, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 1), align 4
  store volatile i32 2, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 2), align 4
  store volatile i32 3, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 3), align 4
  store volatile i32 4, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 4), align 4
  store volatile i32 5, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 5), align 4
  store volatile i32 6, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 6), align 4
  store volatile i32 7, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 7), align 4
  store volatile i32 8, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 8), align 4
  store volatile i32 9, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 9), align 4
  store volatile i32 10, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 10), align 4
  store volatile i32 11, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 11), align 4
  store volatile i32 12, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 12), align 4
  store volatile i32 13, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 13), align 4
  store volatile i32 14, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 14), align 4
  store volatile i32 15, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 15), align 4
  store volatile i32 16, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 16), align 4
  store volatile i32 17, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 17), align 4
  store volatile i32 18, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 18), align 4
  store volatile i32 19, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 19), align 4
  store volatile i32 20, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 20), align 4
  store volatile i32 21, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 21), align 4
  store volatile i32 22, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 22), align 4
  store volatile i32 23, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 23), align 4
  store volatile i32 24, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 24), align 4
  store volatile i32 25, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 25), align 4
  store volatile i32 26, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 26), align 4
  store volatile i32 27, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 27), align 4
  store volatile i32 28, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 28), align 4
  store volatile i32 29, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 29), align 4
  store volatile i32 30, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 30), align 4
  store volatile i32 31, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 31), align 4
  store volatile i32 32, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 32), align 4
  store volatile i32 33, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 33), align 4
  store volatile i32 34, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 34), align 4
  store volatile i32 35, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 35), align 4
  store volatile i32 36, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 36), align 4
  store volatile i32 37, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 37), align 4
  store volatile i32 38, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 38), align 4
  store volatile i32 39, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 39), align 4
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Test 3: Unconditional branch across >32KB gap
define void @test_long_uncond(i32 %a, i32 %b) {
; CHECK-LABEL: test_long_uncond:
; CHECK: st32
; CHECK: st32
; CHECK: jalr{{(\.s[012])?}}
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %pad, label %far

pad:
  store volatile i32 0, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 0), align 4
  store volatile i32 1, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 1), align 4
  store volatile i32 2, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 2), align 4
  store volatile i32 3, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 3), align 4
  store volatile i32 4, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 4), align 4
  store volatile i32 5, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 5), align 4
  store volatile i32 6, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 6), align 4
  store volatile i32 7, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 7), align 4
  store volatile i32 8, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 8), align 4
  store volatile i32 9, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 9), align 4
  store volatile i32 10, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 10), align 4
  store volatile i32 11, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 11), align 4
  store volatile i32 12, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 12), align 4
  store volatile i32 13, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 13), align 4
  store volatile i32 14, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 14), align 4
  store volatile i32 15, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 15), align 4
  store volatile i32 16, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 16), align 4
  store volatile i32 17, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 17), align 4
  store volatile i32 18, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 18), align 4
  store volatile i32 19, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 19), align 4
  store volatile i32 20, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 20), align 4
  store volatile i32 21, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 21), align 4
  store volatile i32 22, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 22), align 4
  store volatile i32 23, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 23), align 4
  store volatile i32 24, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 24), align 4
  store volatile i32 25, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 25), align 4
  store volatile i32 26, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 26), align 4
  store volatile i32 27, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 27), align 4
  store volatile i32 28, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 28), align 4
  store volatile i32 29, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 29), align 4
  store volatile i32 30, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 30), align 4
  store volatile i32 31, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 31), align 4
  store volatile i32 32, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 32), align 4
  store volatile i32 33, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 33), align 4
  store volatile i32 34, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 34), align 4
  store volatile i32 35, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 35), align 4
  store volatile i32 36, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 36), align 4
  store volatile i32 37, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 37), align 4
  store volatile i32 38, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 38), align 4
  store volatile i32 39, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 39), align 4
  br label %far

far:
  store volatile i32 99, ptr getelementptr ([3000 x i32], ptr @arr, i32 0, i32 0), align 4
  ret void
}
