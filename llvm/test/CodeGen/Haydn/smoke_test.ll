; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Simple add
define i32 @test_add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: test_add:
; CHECK: add32
