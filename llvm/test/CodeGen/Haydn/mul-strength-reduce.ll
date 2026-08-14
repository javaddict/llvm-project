; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s32 mul-by-constant. Pure pow2 uses generic mul_to_shl
; (SLLI). pow2±1 stays as MULL: Haydn has 1-slot MULL, so SHL+ADD/SUB is two
; ALU ops vs one MAC.

; Tests for s32 mul-by-constant after deleting Haydn mul_to_shift_add/sub.
; Generic Combine.td mul_to_shl still converts G_MUL x, pow2 -> G_SHL.
; pow2±1 is left as G_MUL and selected to MULL.

;===--- pow2+1: x * 9 stays MULL (not (x << 3) + x) ---===

define i32 @mul_pow2_plus1_9(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_plus1_9:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 9
  ret i32 %r
}

;===--- pow2-1: x * 7 stays MULL (not (x << 3) - x) ---===

define i32 @mul_pow2_minus1_7(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_minus1_7:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 7
  ret i32 %r
}

;===--- x * 3 stays MULL (3 = 4-1 would have been shift-sub) ---===

define i32 @mul_3_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_3_shift_sub:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 3
  ret i32 %r
}

;===--- pow2+1 with 5: x * 5 stays MULL ---===

define i32 @mul_5_shift_add(i32 %x) nounwind {
; CHECK-LABEL: mul_5_shift_add:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 5
  ret i32 %r
}

;===--- pow2-1 with 15: x * 15 stays MULL ---===

define i32 @mul_15_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_15_shift_sub:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 15
  ret i32 %r
}

;===--- pow2+1 with 17: x * 17 stays MULL ---===

define i32 @mul_17_shift_add(i32 %x) nounwind {
; CHECK-LABEL: mul_17_shift_add:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 17
  ret i32 %r
}

;===--- pow2-1 with 31: x * 31 stays MULL ---===

define i32 @mul_31_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_31_shift_sub:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 31
  ret i32 %r
}

;===--- commutative: 9 * x stays MULL ---===

define i32 @mul_pow2_plus1_commuted(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_plus1_commuted:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 9, %x
  ret i32 %r
}

;===--- commutative: 7 * x stays MULL ---===

define i32 @mul_pow2_minus1_commuted(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_minus1_commuted:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 7, %x
  ret i32 %r
}

;===--- negative: 6 is neither (pow2+1) nor (pow2-1), stays as mul ---===

define i32 @mul_6_no_combine(i32 %x) nounwind {
; CHECK-LABEL: mul_6_no_combine:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 6
  ret i32 %r
}

;===--- negative: 11 is neither (pow2+1) nor (pow2-1), stays as mul ---===

define i32 @mul_11_no_combine(i32 %x) nounwind {
; CHECK-LABEL: mul_11_no_combine:
; CHECK: mull
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 11
  ret i32 %r
}

;===--- s64 mul: native schoolbook (no __muldi3 libcall) ---===
; True 64x64 multiply lowers to unsigned widening partials plus shift+add —
; NOT a __muldi3 libcall. Do NOT re-introduce an expectation of __muldi3.

define i64 @mul_s64_no_shift(i64 %x) nounwind {
; CHECK-LABEL: mul_s64_no_shift:
; CHECK-NOT: __muldi3
; CHECK: mul64.ulul{{.*}}
  %r = mul i64 %x, 9
  ret i64 %r
}

;===--- identity: 1 is handled by right_identity_one_int, not shift-add ---===

define i32 @mul_1_identity(i32 %x) nounwind {
; CHECK-LABEL: mul_1_identity:
; CHECK-NOT: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 1
  ret i32 %r
}
