; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — division and remainder operations.

; Test division and remainder operations.
; These should lower to libcalls since Haydn has no hardware div/rem.

;i32 division

define i32 @sdiv_i32(i32 %a, i32 %b) {
; CHECK-LABEL: sdiv_i32:
; CHECK: jal{{(\.s[012])?}} lr, __divsi3
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @udiv_i32(i32 %a, i32 %b) {
; CHECK-LABEL: udiv_i32:
; CHECK: jal{{(\.s[012])?}} lr, __udivsi3
  %r = udiv i32 %a, %b
  ret i32 %r
}

;i32 remainder
define i32 @srem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: srem_i32:
; CHECK: jal{{(\.s[012])?}} lr, __modsi3
  %r = srem i32 %a, %b
  ret i32 %r
}

define i32 @urem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: urem_i32:
; CHECK: jal{{(\.s[012])?}} lr, __umodsi3
  %r = urem i32 %a, %b
  ret i32 %r
}

;i64 division (should also lower to libcalls)
define i64 @sdiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: sdiv_i64:
; CHECK: jal{{(\.s[012])?}} lr, __divdi3
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @udiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: udiv_i64:
; CHECK: jal{{(\.s[012])?}} lr, __udivdi3
  %r = udiv i64 %a, %b
  ret i64 %r
}

;i64 remainder
define i64 @srem_i64(i64 %a, i64 %b) {
; CHECK-LABEL: srem_i64:
; CHECK: jal{{(\.s[012])?}} lr, __moddi3
  %r = srem i64 %a, %b
  ret i64 %r
}

define i64 @urem_i64(i64 %a, i64 %b) {
; CHECK-LABEL: urem_i64:
; CHECK: jal{{(\.s[012])?}} lr, __umoddi3
  %r = urem i64 %a, %b
  ret i64 %r
}

;Combined div and mod
define i32 @divmod_i32(i32 %a, i32 %b) {
; CHECK-LABEL: divmod_i32:
; CHECK: jal{{(\.s[012])?}} lr, __divsi3
; CHECK: jal{{(\.s[012])?}} lr, __modsi3
  %div = sdiv i32 %a, %b
  %rem = srem i32 %a, %b
  %r = add i32 %div, %rem
  ret i32 %r
}

;Division by power of 2 (optimized to arithmetic shifts via division strength reduction)
define i32 @div_pow2(i32 %a) {
; CHECK-LABEL: div_pow2:
; CHECK-NOT: __divsi3
; CHECK: {{sra32|srai32}}
; CHECK: {{sra32|srai32}}
  %r = sdiv i32 %a, 8
  ret i32 %r
}

;Constant division
define i32 @sdiv_const(i32 %a) {
; CHECK-LABEL: sdiv_const:
; May optimize to multiply+shift magic
  %r = sdiv i32 %a, 17
  ret i32 %r
}
