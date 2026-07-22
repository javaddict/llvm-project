; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Multiply / divide / remainder selection smoke.
; s32 mul → MULL; s64 mul → native MUL64 schoolbook or partials; div/rem → libcalls.

define i32 @mul_i32(i32 %a, i32 %b) {
; CHECK-LABEL: mul_i32:
; CHECK: mull
; CHECK-NOT: mul64.ll
  %r = mul i32 %a, %b
  ret i32 %r
}

define i32 @mul_i32_const(i32 %a) {
; CHECK-LABEL: mul_i32_const:
; CHECK: mull
  %r = mul i32 %a, 42
  ret i32 %r
}

define i64 @mul_i64_with_use(i64 %a, i64 %b) {
; CHECK-LABEL: mul_i64_with_use:
; CHECK-DAG: {{mul64|__muldi3}}
  %r = mul i64 %a, %b
  %r2 = add i64 %r, 1
  ret i64 %r2
}

define i32 @sdiv_i32(i32 %a, i32 %b) {
; CHECK-LABEL: sdiv_i32:
; CHECK: __divsi3
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @udiv_i32(i32 %a, i32 %b) {
; CHECK-LABEL: udiv_i32:
; CHECK: __udivsi3
  %r = udiv i32 %a, %b
  ret i32 %r
}

define i32 @srem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: srem_i32:
; CHECK: __modsi3
  %r = srem i32 %a, %b
  ret i32 %r
}

define i32 @urem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: urem_i32:
; CHECK: __umodsi3
  %r = urem i32 %a, %b
  ret i32 %r
}

define i32 @sdiv_const(i32 %a) {
; CHECK-LABEL: sdiv_const:
; CHECK: sra
  %r = sdiv i32 %a, 8
  ret i32 %r
}

define i32 @udiv_const(i32 %a) {
; CHECK-LABEL: udiv_const:
; Magic high-multiply: now uses MULUUH (G_UMULH s32) when available.
; CHECK: {{muluuh|mul64}}
  %r = udiv i32 %a, 10
  ret i32 %r
}

define i32 @srem_const(i32 %a) {
; CHECK-LABEL: srem_const:
; May strength-reduce (and/mask) or call __modsi3.
; CHECK-DAG: {{__modsi3|and32|sra32|srl32}}
  %r = srem i32 %a, 7
  ret i32 %r
}

define i32 @mul_then_div(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: mul_then_div:
; CHECK: mull
; CHECK: __divsi3
  %prod = mul i32 %a, %b
  %r = sdiv i32 %prod, %c
  ret i32 %r
}

define i32 @div_then_mul(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: div_then_mul:
; CHECK: __divsi3
; CHECK: mull
  %quot = sdiv i32 %a, %b
  %r = mul i32 %quot, %c
  ret i32 %r
}

define i32 @modulo_i32(i32 %a, i32 %n) {
; CHECK-LABEL: modulo_i32:
; CHECK: __modsi3
  %r = srem i32 %a, %n
  ret i32 %r
}

define i32 @abs_i32(i32 %a) {
; CHECK-LABEL: abs_i32:
; CHECK: {{sub32|neg|slt|movt|movf}}
  %cmp = icmp slt i32 %a, 0
  %neg = sub i32 0, %a
  %r = select i1 %cmp, i32 %neg, i32 %a
  ret i32 %r
}

define i32 @mul_add(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: mul_add:
; CHECK: mull
; CHECK: add32
  %prod = mul i32 %a, %b
  %r = add i32 %prod, %c
  ret i32 %r
}

define i32 @complex_arith(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: complex_arith:
; CHECK: mull
; CHECK: __divsi3
; CHECK: __modsi3
  %prod = mul i32 %a, %b
  %quot = sdiv i32 %prod, %c
  %rem = srem i32 %a, %c
  %r = add i32 %quot, %rem
  ret i32 %r
}
