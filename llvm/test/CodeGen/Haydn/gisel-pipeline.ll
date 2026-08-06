; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — that the full GISel pipeline works end-to-end.

; Test that the full GISel pipeline works end-to-end

define i32 @test_gisel_pipeline(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: test_gisel_pipeline:
; CHECK: add32
