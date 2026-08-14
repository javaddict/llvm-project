; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: G_MUL by (pow2±1) must stay as MULL, not SHL+ADD/SUB.
;
; Bug: HaydnPostLegalizerCombiner mul_to_shift_add/sub rewrote x*9 to
; (x<<3)+x and x*7 to (x<<3)-x. On a DSP with 1-slot MULL that is two ALU
; ops vs one MAC issue — an unconditional pessimization. Generic mul_to_shl
; (pure pow2) is kept: SLLI is one ALU vs one MAC and packs on ALU0/1/2.
;
; Test design: i32 mul by 9/7 must print mull and must not print a shift.
; If the combiner rules return, CHECK-NOT: {{sll32|slli32}} fails.

; CHECK-LABEL: mul9_stays_mull:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
define i32 @mul9_stays_mull(i32 %x) {
  %r = mul i32 %x, 9
  ret i32 %r
}

; CHECK-LABEL: mul7_stays_mull:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
define i32 @mul7_stays_mull(i32 %x) {
  %r = mul i32 %x, 7
  ret i32 %r
}

; Pure pow2 still strength-reduces via generic mul_to_shl.
; CHECK-LABEL: mul8_is_shift:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
define i32 @mul8_is_shift(i32 %x) {
  %r = mul i32 %x, 8
  ret i32 %r
}
