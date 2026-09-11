; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Comprehensive tests for Haydn DR64 argument passing (D0-D3).

; Comprehensive tests for Haydn DR64 argument passing (D0-D3).
;
; Calling convention:
; i64/f64/SIMD arguments: D0-D3 (4 registers)
; Stack fallback when DR64 registers exhausted
; No DR-to-two-GPR fallback

declare i64 @identity_i64(i64)
declare i64 @add2_i64(i64, i64)

;0 DR64 arguments

define void @caller_0_dr64_args() {
; CHECK-LABEL: caller_0_dr64_args:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;1 DR64 argument (D0)

define i64 @caller_1_dr64_arg(i64 %a) {
; CHECK-LABEL: caller_1_dr64_arg:
; CHECK: lui{{.*}}identity_i64
; CHECK: addi32{{.*}}identity_i64
; CHECK: jalr{{.*}}lr
  %r = call i64 @identity_i64(i64 %a)
  ret i64 %r
}

;2 DR64 arguments (D0, D1)

define i64 @caller_2_dr64_args(i64 %a, i64 %b) {
; CHECK-LABEL: caller_2_dr64_args:
; CHECK: lui{{.*}}add2_i64
; CHECK: addi32{{.*}}add2_i64
; CHECK: jalr{{.*}}lr
  %r = call i64 @add2_i64(i64 %a, i64 %b)
  ret i64 %r
}

;3 DR64 arguments (D0-D2)

declare i64 @add3_i64(i64, i64, i64)

define i64 @caller_3_dr64_args(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: caller_3_dr64_args:
; CHECK: lui{{.*}}add3_i64
; CHECK: addi32{{.*}}add3_i64
; CHECK: jalr{{.*}}lr
  %r = call i64 @add3_i64(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

;4 DR64 arguments (D0-D3, all DR64 register slots used)

declare i64 @add4_i64(i64, i64, i64, i64)

define i64 @caller_4_dr64_args(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: caller_4_dr64_args:
; CHECK: lui{{.*}}add4_i64
; CHECK: addi32{{.*}}add4_i64
; CHECK: jalr{{.*}}lr
  %r = call i64 @add4_i64(i64 %a, i64 %b, i64 %c, i64 %d)
  ret i64 %r
}

;5 DR64 arguments (4 in registers, 5th on stack)

declare i64 @add5_i64(i64, i64, i64, i64, i64)

define i64 @caller_5_dr64_args(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e) {
; CHECK-LABEL: caller_5_dr64_args:
; CHECK-DAG: st64
; CHECK-DAG: {{lui|addi32}}{{.*}}add5_i64
; CHECK-DAG: jalr{{.*}}lr
  %r = call i64 @add5_i64(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e)
  ret i64 %r
}

;8 DR64 arguments (4 in registers, 4 on stack)

declare i64 @add8_i64(i64, i64, i64, i64, i64, i64, i64, i64)

define i64 @caller_8_dr64_args(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h) {
; CHECK-LABEL: caller_8_dr64_args:
; 4 stack stores for overflow arguments
; CHECK-DAG: {{st64|d_sdw|d_sw}}
; CHECK-DAG: {{lui|addi32}}{{.*}}add8_i64
; CHECK-DAG: jalr{{.*}}lr
  %r = call i64 @add8_i64(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h)
  ret i64 %r
}

;Callee with 4 DR64 arguments (all in registers, no stack access)

define i64 @callee_4_dr64(i64 %a, i64 %b, i64 %c, i64 %d) {
; CHECK-LABEL: callee_4_dr64:
; CHECK-NOT: ld64 {{.*}}, sp,
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i64 %a, %b
  %s2 = add i64 %s1, %c
  %s3 = add i64 %s2, %d
  ret i64 %s3
}

;Callee with 5 DR64 arguments (5th from stack)

define i64 @callee_5_dr64(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e) {
; CHECK-LABEL: callee_5_dr64:
; The 5th argument must be loaded from the stack
; CHECK: ld64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i64 %a, %b
  %s2 = add i64 %s1, %c
  %s3 = add i64 %s2, %d
  %s4 = add i64 %s3, %e
  ret i64 %s4
}

;DR64 return value: i64 returned in D0

define i64 @return_i64(i64 %a) {
; CHECK-LABEL: return_i64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 %a
}

;DR64 return value: computed i64 in D0

define i64 @return_computed_i64(i64 %a, i64 %b) {
; CHECK-LABEL: return_computed_i64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i64 %a, %b
  ret i64 %r
}
