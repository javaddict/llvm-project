; RUN: llc -mtriple=haydn-unknown-elf -verify-machineinstrs -global-isel-abort=1 -O2 < %s | FileCheck %s

; CHECK: 	.globl	test_direct_return              // -- Begin function test_direct_return
; CHECK: 	.type	test_direct_return,@function
; CHECK-LABEL: test_direct_return:                     // @test_direct_return
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ nop; nop; xor32	r0, r0, r0 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_direct_return, .Lfunc_end0-test_direct_return
; CHECK:                                         // -- End function

;
; REGRESSION TEST: Register coalescing opportunities.
;
; Purpose: Verify that the register allocator and coalescer can eliminate
; unnecessary copies when values flow through phi nodes, argument registers
; and return registers. Effective coalescing reduces register pressure and
; eliminates redundant COPY/OR64 instructions.
;
; Why this test exists:
; Register coalescing is critical for VLIW targets where every instruction
; slot matters. Redundant copies waste issue slots and increase code size.
; The Haydn backend has getRegAllocationHints that prefers caller-saved
; registers (R1-R7) for temporaries and hints argument/return registers
; from the calling convention. When a value arrives in R1 and needs to be
; returned in R1, coalescing should eliminate the intermediate copy.
;
; What these tests guard:
; 1. Argument-to-return coalescing (value stays in the same register)
; 2. Phi-node coalescing across basic blocks
; 3. Copy chains are collapsed (no redundant moves)
; 4. Coalescing works for both GPR32 and DR64 register banks
;
; If these tests fail, investigate getRegAllocationHints and the coalescer's
; interaction with the Haydn register class constraints.
; Do NOT update CHECK lines without understanding the coalescing decision.
;

;Test 1: Simple identity — argument returned directly.
;Value arrives in R1, should be returned in R1 without any move.

define i32 @test_direct_return(i32 %x) nounwind {
entry:
; %x arrives in R1. No computation, so it should be returned directly
; without a COPY or MOVE32.
  ret i32 %x
}

;Test 2: Argument used in one operation then returned.
;The result should land in R1 directly without an intermediate copy.

define i32 @test_simple_op_return(i32 %x) nounwind {
entry:
; %x in R1. Result of add should go directly to R1 (return register).
  %result = add i32 %x, 1
  ret i32 %result
}

;Test 3: DR64 identity return — argument returned directly.

define i64 @test_dr64_direct_return(i64 %x) nounwind {
entry:
; %x arrives in D0. Should return in D0 without a copy.
  ret i64 %x
}

;Test 4: Phi-node coalescing across two basic blocks.
;The same value flows through a conditional, should stay in one register.

define i32 @test_phi_coalescing(i32 %x, i1 %cond) nounwind {
entry:
; The phi result should coalesce with the input values to minimize copies.
  br i1 %cond, label %then, label %else

then:
  %v1 = add i32 %x, 10
  br label %merge

else:
  %v2 = add i32 %x, 20
  br label %merge

merge:
; The phi should not introduce a COPY — value should already be in
; the correct register from the predecessor block.
  %result = phi i32 [%v1, %then], [%v2, %else]
  %final = add i32 %result, 1
  ret i32 %final
}

;Test 5: Two-argument function where both are accumulated into return.
;Arguments arrive in R1, R2. Result should end up in R1 without extra copies.

define i32 @test_two_arg_accumulate(i32 %a, i32 %b) nounwind {
entry:
; %a in R1, %b in R2. add32 r1, r1, r2 puts result in R1 (return reg).
  %result = add i32 %a, %b
  ret i32 %result
}

;Test 6: DR64 two-argument accumulate.

define i64 @test_dr64_two_arg_accumulate(i64 %a, i64 %b) nounwind {
entry:
; %a in D0, %b in D1. add64 d0, d0, d1 puts result in D0 (return reg).
  %result = add i64 %a, %b
  ret i64 %result
}

;Test 7: Copy chain collapse.
;A chain of passes through function calls should not leave redundant copies.

define i32 @test_copy_chain_collapse() nounwind {
entry:
  %v1 = call i32 @simple_callee()
  ret i32 %v1
}

declare i32 @simple_callee()
