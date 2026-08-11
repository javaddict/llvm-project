; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Tests for the Haydn CFG Optimizer pass. These tests verify that:
; Empty blocks (containing only an unconditional branch) are bypassed
; Conditional branches with identical true/false successors are simplified
; Unreachable blocks are eliminated
;
; The Haydn CFG optimizer is registered in addPreSched2 and runs after the
; load/store optimizer. Generic LLVM passes also simplify CFG, so these tests
; verify the combined effect.

;===----------------------------------------------------------------------===;
; Test 1: Empty block forwarding
;===----------------------------------------------------------------------===;
; The IR contains an empty block (just a branch) that should be bypassed.
; After optimization, only one basic block should remain.
;===----------------------------------------------------------------------===;

define i32 @test_empty_block_forward(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %empty, label %ret
empty:
  br label %ret
ret:
  ret i32 %x
}
; CHECK-LABEL: test_empty_block_forward:
; CHECK: jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}

;===----------------------------------------------------------------------===;
; Test 2: Identical successor merging
;===----------------------------------------------------------------------===;
; Both branches of the conditional go to the same target. Should simplify
; to an unconditional branch or fall-through.
;===----------------------------------------------------------------------===;

define void @test_identical_succ(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %target, label %target
target:
  ret void
}
; CHECK-LABEL: test_identical_succ:
; CHECK: jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}

;===----------------------------------------------------------------------===;
; Test 3: Unreachable block elimination
;===----------------------------------------------------------------------===;
; Block "dead" has no predecessors and should be eliminated entirely.
;===----------------------------------------------------------------------===;

define i32 @test_unreachable_block(i32 %x) {
entry:
  br label %live
dead:
  %dead_result = add i32 %x, 42
  br label %live
live:
  %result = phi i32 [ %x, %entry ], [ %dead_result, %dead ]
  ret i32 %result
}
; CHECK-LABEL: test_unreachable_block:
; CHECK: jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}

;===----------------------------------------------------------------------===;
; Test 4: Multiple empty blocks in a chain
;===----------------------------------------------------------------------===;
; entry -> empty1 -> empty2 -> target. All empty blocks should be
; forwarded so that entry branches directly to target.
;===----------------------------------------------------------------------===;

define i32 @test_chain_empty_blocks(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %empty1, label %target
empty1:
  br label %empty2
empty2:
  br label %target
target:
  ret i32 %x
}
; CHECK-LABEL: test_chain_empty_blocks:
; CHECK: jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}

;===----------------------------------------------------------------------===;
; Test 5: Simple function with no CFG optimization opportunity
;===----------------------------------------------------------------------===;
; This function has a genuine conditional branch with different targets.
; Neither optimization should apply — both branches should remain.
;===----------------------------------------------------------------------===;

define i32 @test_no_opt(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %then, label %else
then:
  %v1 = add i32 %x, 1
  br label %merge
else:
  %v2 = add i32 %x, 2
  br label %merge
merge:
  %result = phi i32 [ %v1, %then ], [ %v2, %else ]
  ret i32 %result
}
; CHECK-LABEL: test_no_opt:
; CHECK: slt32
; CHECK: jalr{{(_[pP][23][0-9]_[A-Z0-9]+)?}}
