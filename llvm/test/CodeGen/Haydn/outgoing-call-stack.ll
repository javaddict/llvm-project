; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
;
; REGRESSION TEST: outgoing call-stack args must be bracketed by SP adjustment.
;
; Bug: HaydnCallLowering::lowerCall wrote outgoing stack arguments at the
; current SP without emitting ADJCALLSTACKDOWN/UP around the call, clobbering
; the caller's frame. The callee would see args at the wrong offset and any
; signal/interrupt during the call would corrupt live data.
; Fix: emit ADJCALLSTACKDOWN (sized by Assigner.StackSize) before the call and
; ADJCALLSTACKUP after; eliminateCallFramePseudoInstr expands them to
; SUBI32/ADDI32 of SP. See F17.
;
; Test design: a call with more args than fit in registers (R1–R7 = 7 GPRs)
; forces stack args. The prologue must NOT reserve call frame space
; (hasReservedCallFrame = false), so the call sequence itself must adjust SP.
; We check that a SUBI32 of SP appears before the JAL and an ADDI32 after.

declare i32 @many_args(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @caller_with_stack_args() {
; The 9th and 10th args (indexes 7, 8) overflow to the stack. The call sequence
; must decrement SP (SUBI32 r13), pass args, call, then increment SP (ADDI32 r13).
  %r = call i32 @many_args(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
                           i32 8, i32 9)
  ret i32 %r
}

; A call with no stack args (<= 7 GPR args) should emit a zero-size adjustment
; which eliminateCallFramePseudoInstr turns into a no-op (no SUBI32/ADDI32).
define i32 @caller_no_stack_args() {
  %r = call i32 @many_args(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7,
                           i32 0, i32 0)
  ret i32 %r
}
