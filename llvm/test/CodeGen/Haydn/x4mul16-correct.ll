; RUN: llc -mtriple=haydn-unknown-elf -global-isel -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Golden X4MUL16 is 2-dest 16x16->32, not G_MUL v4i16.

; Golden X4MUL16 is 2-dest 16x16->32, not G_MUL v4i16.
; Intrinsic path must emit x4mul16; elementwise mul must NOT.
; Also lock golden shapes for related X2/X4 mul family fixes:
;   - X2MULAPL32 etc. are tied-acc MAC (ternary), not binary mul
;   - X4CMUL16 returns 32-bit real/imag (v2i32), not v4i16

declare { i64, i64 } @llvm.haydn.x4mul16(<4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, <4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulapl32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x4cmul16(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s.h(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmula16s.h(<2 x i32>, <4 x i16>, <4 x i16>)

define { i64, i64 } @test_x4mul16_intrinsic(<4 x i16> %a, <4 x i16> %b) {
  %r = call { i64, i64 } @llvm.haydn.x4mul16(<4 x i16> %a, <4 x i16> %b)
  ret { i64, i64 } %r
}

; CHECK-LABEL: test_x4mula16_intrinsic:
; CHECK: x4mula16
define { i64, i64 } @test_x4mula16_intrinsic(i64 %acc1, i64 %acc2, <4 x i16> %a, <4 x i16> %b) {
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc1, i64 %acc2, <4 x i16> %a, <4 x i16> %b)
  ret { i64, i64 } %r
}

; Elementwise G_MUL v4i16 must not use x4mul16 (wrong width / 2-dest).
; CHECK-LABEL: test_g_mul_v4i16:
; CHECK-NOT: x4mul16
define <4 x i16> @test_g_mul_v4i16(<4 x i16> %a, <4 x i16> %b) {
  %r = mul <4 x i16> %a, %b
  ret <4 x i16> %r
}

; Elementwise G_MUL v2i32 uses X2MULPL32 (low 32 of each product), not X2MUL32.
; CHECK-LABEL: test_g_mul_v2i32:
; CHECK: x2mulpl32
; CHECK-NOT: x2mul32
define <2 x i32> @test_g_mul_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %r = mul <2 x i32> %a, %b
  ret <2 x i32> %r
}

; Golden X2MULAPL32: ternary tied-acc MAC.
; CHECK-LABEL: test_x2mulapl32:
; CHECK: x2mulapl32
define <2 x i32> @test_x2mulapl32(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2mulapl32(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2mulaph32:
; CHECK: x2mulaph32
define <2 x i32> @test_x2mulaph32(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

; Golden X4CMUL16: unary, products are 32-bit -> v2i32.
; CHECK-LABEL: test_x4cmul16:
; CHECK: {{x4cmul16[^s]}}
define <2 x i32> @test_x4cmul16(<4 x i16> %a) {
  %r = call <2 x i32> @llvm.haydn.x4cmul16(<4 x i16> %a)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x4cmul16s_h:
; CHECK: x4cmul16s_h
define <2 x i32> @test_x4cmul16s_h(<4 x i16> %a, <4 x i16> %b) {
  %r = call <2 x i32> @llvm.haydn.x4cmul16s.h(<4 x i16> %a, <4 x i16> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x4cmula16s_h:
; CHECK: x4cmula16s_h
define <2 x i32> @test_x4cmula16s_h(<2 x i32> %acc, <4 x i16> %a, <4 x i16> %b) {
  %r = call <2 x i32> @llvm.haydn.x4cmula16s.h(<2 x i32> %acc, <4 x i16> %a, <4 x i16> %b)
  ret <2 x i32> %r
}
