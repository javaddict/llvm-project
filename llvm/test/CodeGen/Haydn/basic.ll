; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Simple add.

; Simple add

define i32 @test_add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: test_add:
; CHECK: add32

; Simple sub
define i32 @test_sub(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: test_sub:
; CHECK: sub32

; Constant
define i32 @test_const() {
  ret i32 42
}
; CHECK-LABEL: test_const:
; CHECK: addi32

; Arithmetic sequence
define i32 @test_arith(i32 %a, i32 %b) {
  %1 = add i32 %a, %b
  %2 = mul i32 %1, %a
  %3 = sub i32 %2, %b
  ret i32 %3
}
; CHECK-LABEL: test_arith:
