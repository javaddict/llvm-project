; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s for mixed GPR+DR64 argument passing.

; Tests for mixed GPR+DR64 argument passing.
;
; Calling convention uses INDEPENDENT cursors for GPR (R1-R7) and DR64 (D0-D3).
; This means i32 and i64 arguments each consume their own register class
; independently — passing an i64 does NOT consume a GPR slot and vice versa.

declare void @sink_i32(i32)
declare void @sink_i64(i64)
declare i32 @add2_i32(i32, i32)
declare i64 @add2_i64(i64, i64)

;1 GPR + 1 DR64 (R1, D0)

define void @mixed_1gpr_1dr64(i32 %a, i64 %b) {
; CHECK-LABEL: mixed_1gpr_1dr64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  call void @sink_i32(i32 %a)
  call void @sink_i64(i64 %b)
  ret void
}

;1 DR64 + 1 GPR (D0, R1) — independent cursors, order doesn't matter

define void @mixed_1dr64_1gpr(i64 %a, i32 %b) {
; CHECK-LABEL: mixed_1dr64_1gpr:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  call void @sink_i64(i64 %a)
  call void @sink_i32(i32 %b)
  ret void
}

;3 GPR + 2 DR64 (R1-R3, D0-D1)

define i32 @mixed_3gpr_2dr64(i32 %a, i32 %b, i32 %c, i64 %d, i64 %e) {
; CHECK-LABEL: mixed_3gpr_2dr64:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  ret i32 %s2
}

;All 7 GPR + 4 DR64 (max register args)

define void @mixed_7gpr_4dr64(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g,
; CHECK-LABEL: mixed_7gpr_4dr64:
; All 7 GPR args in registers, all 4 DR64 args in registers
; No stack argument access for the named arguments (callee-saves do use stack)
; Address of each sink is materialized once and reused.
; CHECK-DAG: {{lui|addi32}}{{.*}}sink_i32
; CHECK-DAG: {{lui|addi32}}{{.*}}sink_i64
; CHECK-DAG: jalr{{.*}}lr
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
                               i64 %h, i64 %i, i64 %j, i64 %k) {
  call void @sink_i32(i32 %a)
  call void @sink_i32(i32 %b)
  call void @sink_i32(i32 %c)
  call void @sink_i32(i32 %d)
  call void @sink_i32(i32 %e)
  call void @sink_i32(i32 %f)
  call void @sink_i32(i32 %g)
  call void @sink_i64(i64 %h)
  call void @sink_i64(i64 %i)
  call void @sink_i64(i64 %j)
  call void @sink_i64(i64 %k)
  ret void
}

;Overflow: 8 GPR + 5 DR64 (1 GPR stack, 1 DR64 stack)

declare void @use_13_args(i32, i32, i32, i32, i32, i32, i32, i32,
                           i64, i64, i64, i64, i64)

define void @mixed_overflow(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h,
; CHECK-LABEL: mixed_overflow:
; Stack stores for overflow arguments
; CHECK-DAG: st32
; CHECK-DAG: st64
; CHECK-DAG: {{lui|addi32}}{{.*}}use_13_args
; CHECK-DAG: jalr{{.*}}lr
                            i64 %i, i64 %j, i64 %k, i64 %l, i64 %m) {
  call void @use_13_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h,
                          i64 %i, i64 %j, i64 %k, i64 %l, i64 %m)
  ret void
}

;Mixed return: GPR returns in R1, DR64 returns in D0

define i32 @return_gpr_from_mixed(i32 %a, i64 %b) {
; CHECK-LABEL: return_gpr_from_mixed:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ext = trunc i64 %b to i32
  %r = add i32 %a, %ext
  ret i32 %r
}

define i64 @return_dr64_from_mixed(i32 %a, i64 %b) {
; CHECK-LABEL: return_dr64_from_mixed:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ext = sext i32 %a to i64
  %r = add i64 %ext, %b
  ret i64 %r
}

;Interleaved GPR/DR64 arguments

define i64 @interleaved_args(i32 %a, i64 %b, i32 %c, i64 %d) {
; CHECK-LABEL: interleaved_args:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %ext_a = sext i32 %a to i64
  %ext_c = sext i32 %c to i64
  %s1 = add i64 %ext_a, %b
  %s2 = add i64 %s1, %ext_c
  %s3 = add i64 %s2, %d
  ret i64 %s3
}

;Call with mixed argument types

define i64 @call_mixed(i32 %a, i64 %b, i32 %c) {
; CHECK-LABEL: call_mixed:
; CHECK: lui{{.*}}add2_i64
; CHECK: addi32{{.*}}add2_i64
; CHECK: jalr{{.*}}lr
  %ext_a = sext i32 %a to i64
  %r = call i64 @add2_i64(i64 %ext_a, i64 %b)
  ret i64 %r
}
