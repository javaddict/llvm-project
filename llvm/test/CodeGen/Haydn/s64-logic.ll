; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Test 64-bit bitwise AND
define i64 @and64(i64 %a, i64 %b) {
; CHECK-LABEL: and64:
; CHECK: and64
  %r = and i64 %a, %b
  ret i64 %r
}

; Test 64-bit bitwise OR
define i64 @or64(i64 %a, i64 %b) {
; CHECK-LABEL: or64:
; CHECK: or64
  %r = or i64 %a, %b
  ret i64 %r
}

; Test 64-bit bitwise XOR
define i64 @xor64(i64 %a, i64 %b) {
; CHECK-LABEL: xor64:
; CHECK: xor64
  %r = xor i64 %a, %b
  ret i64 %r
}

; Test 64-bit AND with immediate
define i64 @and64_imm(i64 %a) {
; CHECK-LABEL: and64_imm:
; CHECK: and64
  %r = and i64 %a, 255
  ret i64 %r
}

; Test chained 64-bit logical operations
define i64 @logic64_chain(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: logic64_chain:
; CHECK: or64
; CHECK: and64
; CHECK: xor64
  %1 = or i64 %a, %b
  %2 = and i64 %1, %c
  %3 = xor i64 %2, %a
  ret i64 %3
}

; Test 64-bit NOT (via XOR with -1)
define i64 @not64(i64 %a) {
; CHECK-LABEL: not64:
; NOT64 logical + FlexMap peer (was silent Pseudo drop).
; CHECK: not64
  %r = xor i64 %a, -1
  ret i64 %r
}
