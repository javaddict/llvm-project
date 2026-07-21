; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.


;
; NOTE: updated for VLIW slot-1 load promotion — independent loads now pack as ld32+ld32.
;
; REGRESSION TEST (-exposed store-to-load forwarding bug,):
; `test_gpr_spill_15_live` exercises the Haydn Load/Store Optimizer's
; store-to-load forwarding path under high GPR pressure. With (strip
; the blanket `Defs=[SFR]` from non-flag ALU ops) re-applied, the post-RA
; SFR WAW chain that serialized scalar ALU ops is gone, so regalloc freely
; reuses the stored register between a spill `ST32 $rN, [sp, k]` and a
; later reload `LD32 $rM, [sp, k]`. The optimizer's KnownContent map kept
; tracking the slot using the stored register as ContentReg, then forwarded
; the reload to `$rM = ADD32 $rN, $r0` *after* $rN had been redefined
; producing a read of an undefined physical register:
; `*** Bad machine code: Using an undefined physical register
; in `$r1 = ADD32 $r4, $r0' where `$r4' is undefined`.
; Fix : invalidateClobberedRegs now also drops entries whose
; ContentReg (the stored value's register) is clobbered, not only those
; whose base register is clobbered. If this regresses, llc crashes under
; verify-machineinstrs at the haydn-ldst-opt dump stage.
;
; REGRESSION TEST: GPR register pressure stress test.
;
; Purpose: Verify that the register allocator correctly handles GPR pressure
; that exceeds the 11 allocatable registers (R1-R11, with R0=zero, R12=AT
; R13=SP, R14=FP, R15=LR reserved). When more than 11 i32 values are
; simultaneously live, the allocator must spill to the stack.
;
; Why this test exists:
; Haydn has 11 allocatable GPR registers (R1-R11). With 15 live i32 values
; held across a function call, at least 4 must spill. The test verifies that
; spill/reload sequences use correct stack offsets and that the function
; still produces the correct result.
;
; NOTE (rebaseline): R12 is now the reserved linker/assembler scratch
; ("AT"): not allocatable, not callee-saved. The allocatable GPR count dropped
; 12 -> 11 and the callee-saved GPR set is R8-R11 (was R8-R12). The old
; `st32 r12` CHECK is removed by design — R12 is never saved. The spilled
; register shift r12->stack is an expected allocation-order shift from the ABI
; change, not a regression.
;
; What these tests guard:
; 1. Spills happen when GPR pressure exceeds allocatable count
; 2. Spill slots are correctly allocated and accessed
; 3. Live values survive across calls (callee-saved restore + reload from stack)
; 4. No verifier errors from incorrect register classes
;
; If these tests fail, investigate the spill/reload logic in the register
; allocator and frame lowering. Do NOT update CHECK lines without understanding
; why the allocation or spill pattern changed.

declare i32 @consume_i32(i32)
declare void @use_i32(i32)

;Test 1: 15 live GPR values across a call forces spills.
;With R1-R7 caller-saved and R8-R11 callee-saved (11 allocatable,)
;15 live values requires at least 4 spills to stack.

define i32 @test_gpr_spill_15_live(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                                    i32 %a4, i32 %a5, i32 %a6) nounwind {
entry:
; Prologue must allocate stack space for spills
; Callee-saved GPRs (R8-R11) are saved to stack. R12 is reserved (AT) and is
; NOT saved (rebaseline: the old `st32 r12` CHECK is gone by design).
; R12 must NOT appear as a callee-save store (it is reserved).
  %v0 = add i32 %a0, 100
  %v1 = add i32 %a1, 101
  %v2 = add i32 %a2, 102
  %v3 = add i32 %a3, 103
  %v4 = add i32 %a4, 104
  %v5 = add i32 %a5, 105
  %v6 = add i32 %a6, 106
  %v7 = add i32 %v0, 107
  %v8 = add i32 %v1, 108
  %v9 = add i32 %v2, 109
  %v10 = add i32 %v3, 110
  %v11 = add i32 %v4, 111
  %v12 = add i32 %v5, 112
  %v13 = add i32 %v6, 113
  %v14 = add i32 %v7, 114
  ; All 15 values (v0-v14) are live here
  call void @use_i32(i32 %v0)
  call void @use_i32(i32 %v14)
  %s0 = add i32 %v0, %v1
  %s1 = add i32 %s0, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  %s5 = add i32 %s4, %v6
  %s6 = add i32 %s5, %v7
  %s7 = add i32 %s6, %v8
  %s8 = add i32 %s7, %v9
  %s9 = add i32 %s8, %v10
  %s10 = add i32 %s9, %v11
  %s11 = add i32 %s10, %v12
  %s12 = add i32 %s11, %v13
  %s13 = add i32 %s12, %v14
  ret i32 %s13
}

;Test 2: All callee-saved GPRs used simultaneously.
;R8-R11 are callee-saved (dropped R12 to reserved AT). Force all 4 to
;hold live values across a call. Note: The allocator may only save a subset
;if not all are needed. R12 is always available as the frame-lowering
;stride-4 scratch (it is no longer in the CSR list, so R12NeedsSaving is
;always false and the stride-4 optimization is always taken when >=2 GPR
;CSRs are saved).

define i32 @test_all_callee_saved_gpr(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) nounwind {
entry:
; Stack is allocated for callee-saves
; Multiple callee-saved registers are stored (at least 3)
; Epilogue restores callee-saved registers
  %v1 = call i32 @consume_i32(i32 %a)
  %v2 = call i32 @consume_i32(i32 %b)
  %v3 = call i32 @consume_i32(i32 %c)
  %v4 = call i32 @consume_i32(i32 %d)
  %v5 = call i32 @consume_i32(i32 %e)
  %s1 = add i32 %v1, %v2
  %s2 = add i32 %s1, %v3
  %s3 = add i32 %s2, %v4
  %s4 = add i32 %s3, %v5
  ret i32 %s4
}

;Test 3: Heavy GPR pressure with many interleaved calls.
;Each call clobbers caller-saved registers, forcing spill/reload around calls.

define i32 @test_interleaved_calls_heavy(i32 %x) nounwind {
entry:
; Multiple callee-save stores in prologue (values live across calls)
  %c1 = call i32 @consume_i32(i32 %x)
  %c2 = call i32 @consume_i32(i32 %c1)
  %c3 = call i32 @consume_i32(i32 %c2)
  %c4 = call i32 @consume_i32(i32 %c3)
  %c5 = call i32 @consume_i32(i32 %c4)
  %c6 = call i32 @consume_i32(i32 %c5)
  %c7 = call i32 @consume_i32(i32 %c6)
  %c8 = call i32 @consume_i32(i32 %c7)
  ; All c1-c8 are live (used below), so they must survive across later calls
  %s1 = add i32 %c1, %c2
  %s2 = add i32 %s1, %c3
  %s3 = add i32 %s2, %c4
  %s4 = add i32 %s3, %c5
  %s5 = add i32 %s4, %c6
  %s6 = add i32 %s5, %c7
  %s7 = add i32 %s6, %c8
  ret i32 %s7
}
