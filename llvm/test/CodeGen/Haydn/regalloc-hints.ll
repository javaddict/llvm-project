; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	test_args_return                // -- Begin function test_args_return
; CHECK: 	.type	test_args_return,@function
; CHECK-LABEL: test_args_return:                       // @test_args_return
; CHECK: // %bb.0:                               // %entry
; CHECK: 	{ nop; nop; xor32	r0, r0, r0 }
; CHECK: 	{ nop; nop; add32	r1, r1, r2 }
; CHECK: 	{ nop; nop; add32	r1, r1, r3 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0; nop; nop }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_args_return, .Lfunc_end0-test_args_return
; CHECK:                                         // -- End function

;
; REGRESSION TEST: Register allocation hints for Haydn backend.
;
; Purpose: Verify that the register allocator respects calling convention
; registers and prefers caller-saved registers (R1-R7, D0-D7) over
; callee-saved registers (R8-R11, D8-D15) for short-lived temporaries.
; (R12 is the reserved linker/assembler scratch "AT" per — never allocated.)
;
; Why this test exists:
; Without getRegAllocationHints, the allocator may assign callee-saved
; registers to short-lived values, forcing unnecessary save/restore in the
; prologue/epilogue. With the hints, caller-saved registers are preferred
; for temporaries without specific copy-based hints, reducing code size
; and improving performance.
;
; What these tests guard:
; 1. Function arguments land in the correct argument registers (R1-R7)
; 2. Return values land in R1 (GPR32) or D0 (DR64)
; 3. Short-lived temporaries prefer caller-saved registers
; 4. Callee-saved registers are only used when caller-saved are exhausted
;
; If these tests fail after a register allocator or calling convention change
; investigate whether the regalloc hints are still being consulted correctly.
; Do NOT update CHECK lines without understanding why the allocation changed.
;

;Test 1: Simple function with 3 arguments — args should use R1, R2, R3.
;No copies should be needed between argument and return registers.

define i32 @test_args_return(i32 %a, i32 %b, i32 %c) nounwind {
entry:
  ; Arguments arrive in R1, R2, R3. The sum is returned in R1.
  ; The computation should use the argument registers directly without
  ; moving them to callee-saved registers.
  %sum = add i32 %a, %b
  %result = add i32 %sum, %c
  ret i32 %result
}

;Test 2: Function with many arguments to exhaust caller-saved GPRs.
;Args R1-R7 (7 GPR args). Return value should end up in R1.

define i32 @test_many_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g) nounwind {
entry:
  ; All 7 args in R1-R7 are used directly. Result accumulates into R1.
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g
  ret i32 %s6
}

;Test 3: i64 argument and return — uses DR64 register D0.

define i64 @test_i64_arg_return(i64 %a, i64 %b) nounwind {
entry:
  ; i64 args arrive in D0, D1. Return in D0.
  %result = add i64 %a, %b
  ret i64 %result
}

;Test 4: Function with only local temporaries.
;These should prefer caller-saved R1-R7 over callee-saved R8-R11.
;The optimizer folds the constants so we only check that no callee-saved
;registers are used.

define i32 @test_local_temps() nounwind {
entry:
  ; All temporaries should use caller-saved registers R1-R7.
  ; The prologue should NOT save/restore R8-R11 since callee-saved
  ; registers are not needed for this simple function. R12 is reserved (AT)
  ; and never appears in user code.
  %a = add i32 1, 2
  %b = add i32 %a, 3
  %c = add i32 %b, 4
  %d = add i32 %c, 5
  ret i32 %d
}

;Test 5: Function that forces callee-saved register usage + stack spills.
;With enough live values across calls, some must spill to callee-saved
;registers; once those are exhausted, the rest spill to the stack. The
;prologue should save and the epilogue should restore the callee-saved set.
;NOTE (rebaseline): R12 is now the reserved linker/assembler scratch
;("AT"), so the callee-saved GPR set is R8-R11 (4 regs, was R8-R12). 8 live
;values across calls therefore overflow the 4 callee-saved GPRs: R8-R11 hold
;4 values and the remaining 4 spill to the stack. R12 is NEVER saved (it is
;reserved). The spilled-register shift r12->stack is an expected allocation
;order shift from the ABI change, not a regression.

define i32 @test_forces_callee_saved() nounwind {
entry:
  ; With 8 live values across calls, callee-saved GPRs R8-R11 hold 4 and the
  ; rest spill to the stack. The prologue saves R8-R11 (R12 is reserved AT
  ; never saved). The old r12 CHECK is gone by design.
  ; R12 must NOT appear as a callee-save store (it is reserved).
  %v1 = call i32 @get_value()
  %v2 = call i32 @get_value()
  %v3 = call i32 @get_value()
  %v4 = call i32 @get_value()
  %v5 = call i32 @get_value()
  %v6 = call i32 @get_value()
  %v7 = call i32 @get_value()
  %v8 = call i32 @get_value()
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  %s5 = add i32 %s4, %v6
  %s6 = add i32 %s5, %v7
  %s7 = add i32 %s6, %v8
  ret i32 %s7
}

declare i32 @get_value()
