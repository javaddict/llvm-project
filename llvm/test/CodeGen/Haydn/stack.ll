; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Fixed-size stack allocation.

; Fixed-size stack allocation

define i32 @test_alloca() {
  %p = alloca i32
  store i32 42, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}
; CHECK-LABEL: test_alloca:
; CHECK: st32
; CHECK: jalr{{(\.s[012])?}}

; Callee-saved registers
define i32 @test_callee_save() {
  ; Force use of many registers to test callee-save
  %v1 = call i32 @extern_func(i32 1)
  %v2 = call i32 @extern_func(i32 2)
  %v3 = call i32 @extern_func(i32 3)
  %r = add i32 %v1, %v2
  %r2 = add i32 %r, %v3
  ret i32 %r2
}
declare i32 @extern_func(i32)
; CHECK-LABEL: test_callee_save:
; CHECK: jal{{(\.s[012])?}}
