; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — generic mul_to_shl (pow2 → SLLI) still applies; pow2±1
; stays MULL (Haydn 1-slot MAC vs two ALU ops).

; Generic Combine.td mul_to_shl: G_MUL x, power_of_2 -> G_SHL x, log2(C).
; SLLI is one ALU vs one MAC and packs on ALU0/1/2.

;===--- mul_by_constant_power_of_2: G_MUL x, 2 -> G_SHL x, 1 ---===

define i32 @mul_by_2(i32 %x) nounwind {
; CHECK-LABEL: mul_by_2:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 2
  ret i32 %r
}

;===--- mul_by_constant_power_of_2: G_MUL x, 4 -> G_SHL x, 2 ---===

define i32 @mul_by_4(i32 %x) nounwind {
; CHECK-LABEL: mul_by_4:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 4
  ret i32 %r
}

;===--- mul_by_constant_power_of_2: G_MUL x, 8 -> G_SHL x, 3 ---===

define i32 @mul_by_8(i32 %x) nounwind {
; CHECK-LABEL: mul_by_8:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 8
  ret i32 %r
}

;===--- mul_by_constant_power_of_2: G_MUL x, 16 -> G_SHL x, 4 ---===

define i32 @mul_by_16(i32 %x) nounwind {
; CHECK-LABEL: mul_by_16:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 16
  ret i32 %r
}

;===--- mul_by_3: 3 = pow2-1; stays MULL (mul_to_shift_sub deleted) ---===

define i32 @mul_by_3(i32 %x) nounwind {
; CHECK-LABEL: mul_by_3:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 3
  ret i32 %r
}

;===--- mul_by_5: 5 = pow2+1; stays MULL (mul_to_shift_add deleted) ---===

define i32 @mul_by_5(i32 %x) nounwind {
; CHECK-LABEL: mul_by_5:
; CHECK: mull
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 5
  ret i32 %r
}

;===--- mul_by_zero: G_MUL x, 0 stays as mul (no mul_zero combine) ---===
; The optimizer constant-folds mul x, 0 to 0, so no mul32 is emitted.

define i32 @mul_by_zero(i32 %x) nounwind {
; CHECK-LABEL: mul_by_zero:
; CHECK-NOT: mul32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 0
  ret i32 %r
}

;===--- mul_by_one: G_MUL x, 1 -> x (identity, not shift) ---===
; Handled by right_identity_one_int (TableGen rule).

define i32 @mul_by_one(i32 %x) nounwind {
; CHECK-LABEL: mul_by_one:
; CHECK-NOT: mul32
; CHECK-NOT: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 1
  ret i32 %r
}

;===--- commutative: G_MUL pow2, x -> G_SHL x, log2(pow2) ---===

define i32 @mul_by_32_commuted(i32 %x) nounwind {
; CHECK-LABEL: mul_by_32_commuted:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 32, %x
  ret i32 %r
}

;===--- large power of 2: G_MUL x, 1073741824 (2^30) ---===

define i32 @mul_by_large_pow2(i32 %x) nounwind {
; CHECK-LABEL: mul_by_large_pow2:
; CHECK-NOT: mul32
; CHECK: {{sll32|slli32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = mul i32 %x, 1073741824
  ret i32 %r
}
