; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s

; Role: semantic — SIMD v2i32 Addition (X2ADD32).

;===----------------------------------------------------------------------===
; SIMD v2i32 Addition (X2ADD32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_add(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_add:
; CHECK: x2add32
  %result = add <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Subtraction (X2SUB32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_sub(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_sub:
; CHECK: x2sub32
  %result = sub <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Multiplication (X2MUL32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_mul(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_mul:
; CHECK: x2mulpl32
  %result = mul <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Bitwise AND (AND64)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_and(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_and:
; CHECK: and64
  %result = and <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Bitwise OR (OR64)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_or(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_or:
; CHECK: or64
  %result = or <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Bitwise XOR (XOR64)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_xor(<2 x i32> %a, <2 x i32> %b) nounwind {
; CHECK-LABEL: simd_v2i32_xor:
; CHECK: xor64
  %result = xor <2 x i32> %a, %b
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; TODO: SIMD Load/Store - need pointer type legalization
;===----------------------------------------------------------------------===
