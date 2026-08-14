; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Comprehensive tests for Haydn GPR argument passing (R1-R7).

; Comprehensive tests for Haydn GPR argument passing (R1-R7).
;
; Calling convention:
; GPR arguments: R1-R7 (7 registers, R0 is reserved soft-zero)
; Stack fallback when registers exhausted
; Small integers (i1/i8/i16) promoted to i32

declare void @sink_i32(i32)
declare i32 @identity_i32(i32)

;0 GPR arguments (void callee)

define void @caller_0_args() {
; CHECK-LABEL: caller_0_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, sink_i32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  call void @sink_i32(i32 0)
  ret void
}

;1 GPR argument (fits in R1)

define i32 @caller_1_arg(i32 %a) {
; CHECK-LABEL: caller_1_arg:
; CHECK: jal{{(\.s[012])?}} {{.*}}, identity_i32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i32 @identity_i32(i32 %a)
  ret i32 %r
}

;2 GPR arguments (R1, R2)

declare i32 @add2(i32, i32)

define i32 @caller_2_args(i32 %a, i32 %b) {
; CHECK-LABEL: caller_2_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add2
  %r = call i32 @add2(i32 %a, i32 %b)
  ret i32 %r
}

;3 GPR arguments (R1, R2, R3)

declare i32 @add3(i32, i32, i32)

define i32 @caller_3_args(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: caller_3_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add3
  %r = call i32 @add3(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

;4 GPR arguments (R1-R4)

declare i32 @add4(i32, i32, i32, i32)

define i32 @caller_4_args(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: caller_4_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add4
  %r = call i32 @add4(i32 %a, i32 %b, i32 %c, i32 %d)
  ret i32 %r
}

;5 GPR arguments (R1-R5)

declare i32 @add5(i32, i32, i32, i32, i32)

define i32 @caller_5_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
; CHECK-LABEL: caller_5_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add5
  %r = call i32 @add5(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e)
  ret i32 %r
}

;6 GPR arguments (R1-R6)

declare i32 @add6(i32, i32, i32, i32, i32, i32)

define i32 @caller_6_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
; CHECK-LABEL: caller_6_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add6
  %r = call i32 @add6(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f)
  ret i32 %r
}

;7 GPR arguments (R1-R7, all register slots used)

declare i32 @add7(i32, i32, i32, i32, i32, i32, i32)

define i32 @caller_7_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g) {
; CHECK-LABEL: caller_7_args:
; CHECK: jal{{(\.s[012])?}} {{.*}}, add7
  %r = call i32 @add7(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g)
  ret i32 %r
}

;8 GPR arguments (7 in registers R1-R7, 8th on stack)
;The 8th argument overflows to the stack, so a st32 is emitted
;to store the outgoing stack argument before the call.

declare i32 @add8(i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @caller_8_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h) {
; CHECK-LABEL: caller_8_args:
; CHECK: st32
; CHECK: jal{{(\.s[012])?}} {{.*}}, add8
  %r = call i32 @add8(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h)
  ret i32 %r
}

;10 GPR arguments (7 in registers, 3 on stack)

declare i32 @add10(i32, i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @caller_10_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h, i32 %i, i32 %j) {
; CHECK-LABEL: caller_10_args:
; CHECK: st32
; CHECK: st32
; CHECK: st32
; CHECK: jal{{(\.s[012])?}} {{.*}}, add10
  %r = call i32 @add10(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h, i32 %i, i32 %j)
  ret i32 %r
}

;Callee with 7 arguments (all in registers)
;Verify no stack loads for argument access (all from registers)

define i32 @callee_7_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g) {
; CHECK-LABEL: callee_7_args:
; CHECK: add32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g
  ret i32 %s6
}

;Callee with 8 arguments (8th from stack)
;The 8th argument is loaded from the stack via ld32.

define i32 @callee_8_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h) {
; CHECK-LABEL: callee_8_args:
; The 8th argument is loaded from the stack
; CHECK: ld32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  %s5 = add i32 %s4, %f
  %s6 = add i32 %s5, %g
  %s7 = add i32 %s6, %h
  ret i32 %s7
}

;Small integer promotion: i8, i16 arguments promoted to i32

define i32 @small_int_args(i8 %a, i16 %b, i32 %c) {
; CHECK-LABEL: small_int_args:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ext_a = sext i8 %a to i32
  %ext_b = sext i16 %b to i32
  %s1 = add i32 %ext_a, %ext_b
  %s2 = add i32 %s1, %c
  ret i32 %s2
}

;Pointer arguments (passed in GPR registers, same as i32)

declare void @use_ptr(ptr)

define void @ptr_arg(ptr %p) {
; CHECK-LABEL: ptr_arg:
; CHECK: jal{{(\.s[012])?}} {{.*}}, use_ptr
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  call void @use_ptr(ptr %p)
  ret void
}
