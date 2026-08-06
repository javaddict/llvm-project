; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — 64-bit addition.

; Test 64-bit addition

define i64 @add64(i64 %a, i64 %b) {
; CHECK-LABEL: add64:
; CHECK: add64
  %r = add i64 %a, %b
  ret i64 %r
}

; Test 64-bit subtraction
define i64 @sub64(i64 %a, i64 %b) {
; CHECK-LABEL: sub64:
; CHECK: sub64
  %r = sub i64 %a, %b
  ret i64 %r
}

; Test 64-bit multiplication (libcall - not yet implemented)
define i64 @mul64(i64 %a, i64 %b) {
; CHECK-LABEL: mul64:
; TODO: Should emit libcall __ashldi3 or similar
  %r = mul i64 %a, %b
  ret i64 %r
}

; Test 64-bit addition with immediate
define i64 @add64_imm(i64 %a) {
; CHECK-LABEL: add64_imm:
; CHECK: add64
  %r = add i64 %a, 42
  ret i64 %r
}

; Test chained 64-bit arithmetic
define i64 @arith64_chain(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: arith64_chain:
; CHECK: add64
; CHECK: sub64
  %1 = add i64 %a, %b
  %2 = sub i64 %1, %c
  ret i64 %2
}
