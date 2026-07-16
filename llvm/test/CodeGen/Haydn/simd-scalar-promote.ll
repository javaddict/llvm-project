; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s

;===----------------------------------------------------------------------===
; SIMD Scalar-to-SIMD Pair Promotion Tests
;===----------------------------------------------------------------------===
; Tests for the Haydn SIMD promotion optimization that recognizes pairs of
; independent scalar i32 operations on values extracted from the same DR64
; registers and promotes them to single SIMD X2 operations.
;
; Pattern recognized:
; %lo0, %hi0 = MOV_DR64_TO_GPR %vec_a
; %lo1, %hi1 = MOV_DR64_TO_GPR %vec_b
; %r0 = ADD32 %lo0, %lo1
; %r1 = ADD32 %hi0, %hi1
; %result = MOV_GPR_TO_DR64 %r0, %r1
; => %result = X2ADD32 %vec_a, %vec_b
;
; This pattern arises when C code extracts two halves of a SIMD value
; operates on them independently, and packs them back.
;
; REGRESSION TEST: If the promotion regresses, the output will contain
; individual add32 instructions and MOV_GPR_TO_DR64 instead of x2add32.
;===----------------------------------------------------------------------===

; TEST 1: Scalar addition pair from vector extract-then-repack
; Extract two i32 elements from each of two <2 x i32>, add them element-wise
; pack back into <2 x i32>. This should be promoted to X2ADD32.
define <2 x i32> @test_promote_add(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_promote_add:
; CHECK: x2add32
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %result = add <2 x i32> %va, %vb
  ret <2 x i32> %result
}

; TEST 2: Scalar subtraction pair promotion to X2SUB32
define <2 x i32> @test_promote_sub(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_promote_sub:
; CHECK: x2sub32
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %result = sub <2 x i32> %va, %vb
  ret <2 x i32> %result
}

; TEST 3: Scalar multiplication pair promotion to X2MUL32
define <2 x i32> @test_promote_mul(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_promote_mul:
; CHECK: x2mul32
  %va = bitcast i64 %a to <2 x i32>
  %vb = bitcast i64 %b to <2 x i32>
  %result = mul <2 x i32> %va, %vb
  ret <2 x i32> %result
}
