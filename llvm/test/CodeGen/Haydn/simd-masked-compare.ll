; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — SFR-predicated SIMD compare/cmov and scalar64 SFR transfer emit native mnemonics (not DCE'd).

; SIMD masked comparison and conditional-move intrinsics must reach assembly.
; Compare ops set per-lane SFR; cmov selects on SFR; scalar64 and SFR transfer
; ops are unary/side-effect forms. Empty bodies (label-only) used to pass when
; these were incorrectly treated as dead; pin each mnemonic.

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) compare -> SFR
;===----------------------------------------------------------------------===


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

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) conditional move based on SFR
;===----------------------------------------------------------------------===

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

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) compare -> SFR
;===----------------------------------------------------------------------===

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

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) conditional move based on SFR
;===----------------------------------------------------------------------===

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

;===----------------------------------------------------------------------===
; Scalar 64-bit SFR compare + conditional move
;===----------------------------------------------------------------------===

; CHECK-LABEL: test_slt64:
; CHECK: slt64
define i64 @test_slt64(i64 %a) {
  %r = call i64 @llvm.haydn.slt64(i64 %a)
  ret i64 %r
}

; CHECK-LABEL: test_sle64:
; CHECK: sle64
define i64 @test_sle64(i64 %a) {
  %r = call i64 @llvm.haydn.sle64(i64 %a)
  ret i64 %r
}

; CHECK-LABEL: test_movt64:
; CHECK: movt64
define i64 @test_movt64(i64 %a) {
  %r = call i64 @llvm.haydn.movt64(i64 %a)
  ret i64 %r
}

; CHECK-LABEL: test_movf64:
; CHECK: movf64
define i64 @test_movf64(i64 %a) {
  %r = call i64 @llvm.haydn.movf64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; SFR register transfer intrinsics
;===----------------------------------------------------------------------===

; CHECK-LABEL: test_movesfr2gpr:
; CHECK: movesfr2gpr
define i32 @test_movesfr2gpr() {
  %r = call i32 @llvm.haydn.movesfr2gpr()
  ret i32 %r
}

; CHECK-LABEL: test_movegpr2sfr:
; CHECK: movegpr2sfr
define void @test_movegpr2sfr(i32 %val) {
  call void @llvm.haydn.movegpr2sfr(i32 %val)
  ret void
}

; CHECK-LABEL: test_zero_sfr:
; CHECK: zero_sfr
define void @test_zero_sfr() {
  call void @llvm.haydn.zero.sfr()
  ret void
}

;===----------------------------------------------------------------------===
; Synthesized compare patterns (GT from swapped SLT)
;
; Haydn does not have unsigned SIMD compares or GT/GE at the SIMD level.
; These are synthesized by swapping operands:
; a > b <=> b < a (use SLT with swapped operands)
; a >= b <=> !(a < b) (use SLT + MOVF instead of MOVT)
;===----------------------------------------------------------------------===

; CHECK-LABEL: test_x2cmp_gt_synthesized:
; CHECK-DAG: x2slt32
; CHECK-DAG: x2movt32
define <2 x i32> @test_x2cmp_gt_synthesized(<2 x i32> %a, <2 x i32> %b, <2 x i32> %src) {
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %b,<2 x i32> %a)
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %src)
  ret <2 x i32> %result
}

; CHECK-LABEL: test_x4cmp_gt_synthesized:
; CHECK-DAG: x4slt16
; CHECK-DAG: x4movt16
define <4 x i16> @test_x4cmp_gt_synthesized(<4 x i16> %a, <4 x i16> %b, <4 x i16> %src) {
  %cmp = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %b,<4 x i16> %a)
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %src)
  ret <4 x i16> %result
}

;===----------------------------------------------------------------------===
; Intrinsic declarations
;===----------------------------------------------------------------------===

; X2 compare (binary DR64)
declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)

; X2 conditional move (binary DR64)
declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

; X4 compare (binary DR64)
declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)

; X4 conditional move (binary DR64)
declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

; Scalar 64-bit SFR (unary DR64)
declare i64 @llvm.haydn.slt64(i64)
declare i64 @llvm.haydn.sle64(i64)
declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

; SFR register transfer
declare i32 @llvm.haydn.movesfr2gpr()
declare void @llvm.haydn.movegpr2sfr(i32)
declare void @llvm.haydn.zero.sfr()
