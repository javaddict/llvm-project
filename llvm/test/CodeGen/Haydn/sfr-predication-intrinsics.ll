; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Wave 3 SFR flag register predication intrinsics.

; REGRESSION TEST: Wave 3 SFR flag register predication intrinsics.
;
; Tests the compare->SFR->conditional-move pattern for SIMD predication.
; Compare ops (X2SEQ32, X2SLT32, X2SLE32, X4SEQ16, X4SLT16, X4SLE16)
; take two DR64 operands and set per-lane SFR flags.
; Move ops (X2MOVF32, X2MOVT32, X4MOVF16, X4MOVT16) take two DR64
; operands and conditionally select per-lane based on SFR flags.
;
; Bug history: The.td definitions originally had these as unary (one DR64
; input) but the ISA spec shows binary (two DR64 inputs: rsd1, rsd2 for
; compares; rtd, rsd for moves). Fixed to match the spec.
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;X2 compare -> SFR (binary DR64)


define <2 x i32> @test_x2seq32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2slt32:
; CHECK: x2slt32
define <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2sle32:
; CHECK: x2sle32
define <2 x i32> @test_x2sle32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;X2 conditional move based on SFR (binary DR64)

; CHECK-LABEL: test_x2movf32:
; CHECK: x2movf32
define <2 x i32> @test_x2movf32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2movt32:
; CHECK: x2movt32
define <2 x i32> @test_x2movt32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

;X4 compare -> SFR (binary DR64)

; CHECK-LABEL: test_x4seq16:
; CHECK: x4seq16
define <4 x i16> @test_x4seq16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4slt16:
; CHECK: x4slt16
define <4 x i16> @test_x4slt16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4sle16:
; CHECK: x4sle16
define <4 x i16> @test_x4sle16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;X4 conditional move based on SFR (binary DR64)

; CHECK-LABEL: test_x4movf16:
; CHECK: x4movf16
define <4 x i16> @test_x4movf16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4movt16:
; CHECK: x4movt16
define <4 x i16> @test_x4movt16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}

;Intrinsics declarations

declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)
