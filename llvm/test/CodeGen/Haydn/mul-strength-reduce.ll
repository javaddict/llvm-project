; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — s for mul-to-shift strength reduction combiner rules in HaydnPostLegalizerCombiner.

; Tests for mul-to-shift strength reduction combiner rules in
; HaydnPostLegalizerCombiner. These combines convert multiplication by
; special constants into cheaper shift/shift-add/shift-sub sequences:
;
; 1. mul_to_shift: G_MUL x, pow2 -> G_SHL x, log2(pow2)
; 2. mul_to_shift_add: G_MUL x, (pow2+1) -> G_ADD (G_SHL x, log2), x
; 3. mul_to_shift_sub: G_MUL x, (pow2-1) -> G_SUB (G_SHL x, log2), x
;
; Only s32 types are transformed. s64 mul is no longer a libcall: G7
; lowers every s64 multiply to a native MUL64_LL schoolbook sequence.
; Note: some constants match both patterns (e.g., 3 = 2+1 = 4-1). The first
; matching pattern wins (shift-sub is checked before shift-add). Both produce
; the correct result.

;===--- pow2+1: x * 9 = (x << 3) + x ---===

define i32 @mul_pow2_plus1_9(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_plus1_9:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{add32|addi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 9
  ret i32 %r
}

;===--- pow2-1: x * 7 = (x << 3) - x ---===

define i32 @mul_pow2_minus1_7(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_minus1_7:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{sub32|subi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 7
  ret i32 %r
}

;===--- x * 3: matches as (pow2-1) where pow2=4: (x << 2) - x = 3x ---===
; 3 = 4-1, so shift-sub fires first (checked before shift-add).

define i32 @mul_3_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_3_shift_sub:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{sub32|subi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 3
  ret i32 %r
}

;===--- pow2+1 with 5: x * 5 = (x << 2) + x ---===

define i32 @mul_5_shift_add(i32 %x) nounwind {
; CHECK-LABEL: mul_5_shift_add:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{add32|addi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 5
  ret i32 %r
}

;===--- pow2-1 with 15: x * 15 = (x << 4) - x ---===

define i32 @mul_15_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_15_shift_sub:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{sub32|subi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 15
  ret i32 %r
}

;===--- pow2+1 with 17: x * 17 = (x << 4) + x ---===

define i32 @mul_17_shift_add(i32 %x) nounwind {
; CHECK-LABEL: mul_17_shift_add:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{add32|addi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 17
  ret i32 %r
}

;===--- pow2-1 with 31: x * 31 = (x << 5) - x ---===

define i32 @mul_31_shift_sub(i32 %x) nounwind {
; CHECK-LABEL: mul_31_shift_sub:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{sub32|subi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 31
  ret i32 %r
}

;===--- commutative: 9 * x = (x << 3) + x ---===

define i32 @mul_pow2_plus1_commuted(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_plus1_commuted:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{add32|addi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 9, %x
  ret i32 %r
}

;===--- commutative: 7 * x = (x << 3) - x ---===

define i32 @mul_pow2_minus1_commuted(i32 %x) nounwind {
; CHECK-LABEL: mul_pow2_minus1_commuted:
; CHECK-NOT: mull
; CHECK: {{sll32|slli32}}
; CHECK: {{sub32|subi32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 7, %x
  ret i32 %r
}

;===--- negative: 6 is neither (pow2+1) nor (pow2-1), stays as mul ---===

define i32 @mul_6_no_combine(i32 %x) nounwind {
; CHECK-LABEL: mul_6_no_combine:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 6
  ret i32 %r
}

;===--- negative: 11 is neither (pow2+1) nor (pow2-1), stays as mul ---===

define i32 @mul_11_no_combine(i32 %x) nounwind {
; CHECK-LABEL: mul_11_no_combine:
; CHECK: mull
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 11
  ret i32 %r
}

;===--- s64 mul: G7 native schoolbook decomposition (no __muldi3 libcall) ---===
; G7 : a true 64x64 multiply now lowers to three MUL64_LL partial
; products (LL/LH/HL) plus shift+add — NOT a __muldi3 libcall. The constant 9
; is not a power-of-two +/- 1 in the 64-bit domain, so the mul-to-shift
; combiner correctly leaves it as a real multiply; the legalizer then emits the
; native schoolbook sequence. This is the correct codegen — do NOT re-introduce
; an expectation of __muldi3.

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
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 1
  ret i32 %r
}
