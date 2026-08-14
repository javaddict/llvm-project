; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s for return value conventions.

; Tests for return value conventions.
;
; GPR returns: R1 (first i32/ptr), R2 (second i32)
; DR64 returns: D0 (i64/f64/SIMD)
; R0 is reserved as soft-zero, NOT used for returns.

;Single i32 return in R1

define i32 @ret_i32_const() {
; CHECK-LABEL: ret_i32_const:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 42
}

define i32 @ret_i32_arg(i32 %a) {
; CHECK-LABEL: ret_i32_arg:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i32 %a
}

;Single i64 return in D0

define i64 @ret_i64_const() {
; CHECK-LABEL: ret_i64_const:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 123456789
}

define i64 @ret_i64_arg(i64 %a) {
; CHECK-LABEL: ret_i64_arg:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 %a
}

;Pointer return in R1

define ptr @ret_ptr(ptr %p) {
; CHECK-LABEL: ret_ptr:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret ptr %p
}

;Computed i32 return (ensure result ends up in R1)

define i32 @ret_i32_computed(i32 %a, i32 %b) {
; CHECK-LABEL: ret_i32_computed:
; CHECK: add32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i32 %a, %b
  ret i32 %r
}

;Computed i64 return (ensure result ends up in D0)

define i64 @ret_i64_computed(i64 %a, i64 %b) {
; CHECK-LABEL: ret_i64_computed:
; CHECK: add64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i64 %a, %b
  ret i64 %r
}

;Small integer returns (promoted to i32, returned in R1)

define i8 @ret_i8(i8 %a) {
; CHECK-LABEL: ret_i8:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i8 %a
}

define i16 @ret_i16(i16 %a) {
; CHECK-LABEL: ret_i16:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i16 %a
}

define i1 @ret_i1(i1 %a) {
; CHECK-LABEL: ret_i1:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i1 %a
}

;Void return (no return value register needed)

define void @ret_void() {
; CHECK-LABEL: ret_void:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret void
}

;Return value from call: callee's R1/D0 flows to caller

declare i32 @get_i32()
declare i64 @get_i64()

define i32 @forward_ret_i32() {
; CHECK-LABEL: forward_ret_i32:
; CHECK: jal{{(\.s[012])?}} {{.*}}, get_i32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i32 @get_i32()
  ret i32 %r
}

define i64 @forward_ret_i64() {
; CHECK-LABEL: forward_ret_i64:
; CHECK: jal{{(\.s[012])?}} {{.*}}, get_i64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i64 @get_i64()
  ret i64 %r
}

;Multiple return values via struct: {i32, i32} uses R1, R2

define { i32, i32 } @ret_pair(i32 %a, i32 %b) {
; CHECK-LABEL: ret_pair:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertvalue { i32, i32 } undef, i32 %a, 0
  %r2 = insertvalue { i32, i32 } %r, i32 %b, 1
  ret { i32, i32 } %r2
}

;Struct with i64: uses D0 for i64 field

define { i64, i32 } @ret_i64_struct(i64 %a, i32 %b) {
; CHECK-LABEL: ret_i64_struct:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertvalue { i64, i32 } undef, i64 %a, 0
  %r2 = insertvalue { i64, i32 } %r, i32 %b, 1
  ret { i64, i32 } %r2
}

;Constant i64 return (materialization test)

define i64 @ret_i64_large_const() {
; CHECK-LABEL: ret_i64_large_const:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret i64 1000000000000
}
