; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — full GISel pipeline end-to-end with MachineVerifier enabled.
; Verifier-enabled GISel is a G-TEST-EVIDENCE / G-BACKEND-CORRECTNESS exit:
; no historical disable.

define i32 @test_gisel_pipeline(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: test_gisel_pipeline:
; CHECK: add32
