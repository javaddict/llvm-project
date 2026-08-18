; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — X2/X4 register-form DR64 shifts (data DR64, shift GPR32)
; must select and emit real mnemonics (not dropped pseudos).

; Cross-bank shift amount: the selector must type-infer the GPR32 shift
; operand (not MRI.getRegBankOrNull, which is null for bank-only vregs) so
; MOV_DR64_TO_GPR is not fed a GPR32 source. Sibling dr64-shift-intrinsics.ll
; covers the broader shift family; both are live PASS contracts.
;
; REGRESSION TEST: DR64 register-form shift intrinsics must round-trip through
; the full CodeGen pipeline to real instructions in the assembly output.
;
; What this test guards:
; X2SLL32, X2SRA32, X2SRL32 (X2 = 2x32-bit lane SIMD)
; X4SLL16, X4SRA16, X4SRL16 (X4 = 4x16-bit lane SIMD)
; X2SRA32R, X4SRA16R (rounding variants)
;
; Each takes (DR64 data, GPR32 shift amount) and returns DR64. The selector
; must emit the cross-bank form. If any regresses to a dropped pseudo, the
; corresponding CHECK line fails because the mnemonic disappears.

;===----------------------------------------------------------------------===;
; X2 SIMD register shifts (binary DR64, GPR32 shift amount)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2sll32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2sra32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2srl32(<2 x i32>, i32)

define dso_local <2 x i32> @test_x2sll32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sll32:
; CHECK: x2sll32
  %r = call <2 x i32> @llvm.haydn.x2sll32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2sra32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32:
; CHECK: x2sra32
  %r = call <2 x i32> @llvm.haydn.x2sra32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2srl32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2srl32:
; CHECK: x2srl32
  %r = call <2 x i32> @llvm.haydn.x2srl32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD register shifts (binary DR64, GPR32 shift amount)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4sll16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4sra16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4srl16(<4 x i16>, i32)

define dso_local <4 x i16> @test_x4sll16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sll16:
; CHECK: x4sll16
  %r = call <4 x i16> @llvm.haydn.x4sll16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4sra16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sra16:
; CHECK: x4sra16
  %r = call <4 x i16> @llvm.haydn.x4sra16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4srl16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4srl16:
; CHECK: x4srl16
  %r = call <4 x i16> @llvm.haydn.x4srl16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2/X4 shift rounding (binary DR64, GPR32 shift amount)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2sra32r(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4sra16r(<4 x i16>, i32)

define dso_local <2 x i32> @test_x2sra32r(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32r:
; CHECK: x2sra32r
  %r = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <4 x i16> @test_x4sra16r(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sra16r:
; CHECK: x4sra16r
  %r = call <4 x i16> @llvm.haydn.x4sra16r(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}
