; UNSUPPORTED: true
; Role: retired — RedundantCopyElim pass deleted; not product coverage.
; Do not count as product green. RUN is deliberately false so a dropped
; RUN: false

; Tests for the HaydnRedundantCopyElim pass (post-RA condition-based elimination).
;
; This pass leverages dominating condition information to eliminate copies whose
; values are implied by the control flow. It handles patterns like:
;
; BEQZ rs,.Ltarget: on taken path, rs is known to be 0
; BNEZ rs,.Ltarget: on fallthrough path, rs is known to be 0
; BEQ rs1, rs2,.Ltarget: on taken path, rs1 == rs2
; BNE rs1, rs2,.Ltarget: on fallthrough path, rs1 == rs2
;
; The pass runs after HaydnCopyElim (which handles identity/dead/R0 copies).

;===--- Basic identity: ensure trivial self-copies are eliminated ---===
; Even though HaydnCopyElim handles identity copies, the redundant copy elim
; should not interfere with functions that have simple returns.

define i32 @test_basic_return(i32 %a) nounwind {
; CHECK-LABEL: test_basic_return:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 %a
}

;===--- Conditional branch with zero: check that the branch path is clean ---===
; When comparing a value against zero and branching, the compiler should
; not emit redundant copies of zero into the tested register on the taken path.
; The is_zero block returns 0 (which is R0), so no copy to R1 is needed
; since on this path R1 is already known to be 0 from the condition.

define i32 @test_beqz_path(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: test_beqz_path:
; On the is_zero path, %a is 0. The redundant copy elim may eliminate
; copies that set the return value to 0 when the condition already implies it.
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, 0
  br i1 %cmp, label %is_zero, label %not_zero

is_zero:
  ret i32 0

not_zero:
  %r = add i32 %a, %b
  ret i32 %r
}

;===--- Conditional branch with equality: test equal-path optimization ---===
; When branching on a == b, on the equal path we know both registers hold
; the same value. The compiler should not emit redundant copies between them.

define i32 @test_beq_path(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: test_beq_path:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %equal, label %notequal

equal:
  ret i32 %a

notequal:
  ret i32 %a
}

;===--- Nested conditions: ensure correctness with multiple branch levels ---===

define i32 @test_nested_conditions(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: test_nested_conditions:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp1 = icmp eq i32 %a, 0
  br i1 %cmp1, label %a_zero, label %a_nonzero

a_zero:
  %cmp2 = icmp eq i32 %b, 0
  br i1 %cmp2, label %both_zero, label %only_a_zero

both_zero:
  ret i32 0

only_a_zero:
  ret i32 %b

a_nonzero:
  %cmp3 = icmp eq i32 %a, %c
  br i1 %cmp3, label %a_eq_c, label %a_ne_c

a_eq_c:
  %r = add i32 %a, %c
  ret i32 %r

a_ne_c:
  ret i32 %c
}

;===--- Phi node with identical values: test that copies are minimized ---===
; When a phi node has the same incoming value from multiple predecessors
; the register allocator may produce copies that the condition analysis
; can prove are redundant.

define i32 @test_phi_same_value(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: test_phi_same_value:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %left, label %right

left:
  br label %join

right:
  br label %join

join:
  %val = phi i32 [ %a, %left ], [ %a, %right ]
  ret i32 %val
}

;===--- Multiple returns with different values: copies must be preserved ---===
; This tests that the pass does NOT eliminate copies that are actually needed.
; Each return path must set the return register correctly.

define i32 @test_preserve_live_copies(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: test_preserve_live_copies:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %ret_a, label %ret_b

ret_a:
  ret i32 %a

ret_b:
  ret i32 %b
}

;===--- Callee-saved register test: CSR restores must not be removed ---===
; Copies restoring callee-saved registers from the stack are live-out to the
; caller and must NOT be eliminated.

define i32 @test_csr_preserve(i32 %a) nounwind {
; CHECK-LABEL: test_csr_preserve:
; CHECK: mull
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %a, %a
  ret i32 %r
}

;===--- Branch on comparison result: SEQ32 + BNEZ pattern ---===
; Haydn lowers icmp eq to SEQ32 + BNEZ. Test that this pattern works
; correctly with the redundant copy elim pass (the pass should not
; incorrectly eliminate needed instructions).

define i32 @test_seq32_bnez(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: test_seq32_bnez:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
entry:
  %cmp = icmp eq i32 %a, %b
  br i1 %cmp, label %eq_path, label %ne_path

eq_path:
  %sum = add i32 %a, %b
  %r1 = add i32 %sum, %c
  ret i32 %r1

ne_path:
  ret i32 %c
}
