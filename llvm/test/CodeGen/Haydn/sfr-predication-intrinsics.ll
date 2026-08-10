; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Wave 3 SFR flag register predication intrinsics.
;
; Golden Format E + BundleSim ISA (slot2_alu.h):
;   X2SEQ32/SLT/SLE rsd1, rsd2     — 2 ops, SFR write only
;   X2MOVT32/MOVF32 rtd, rsd       — 2 ops, RMW on rtd
; Same for X4*16. Reject 3-register dumps (CB-137 class).

; CHECK-LABEL: test_x2seq32:
; CHECK: x2seq32 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x2seq32 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <2 x i32> @test_x2seq32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2slt32:
; CHECK: x2slt32 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x2slt32 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2sle32:
; CHECK: x2sle32 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x2sle32 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <2 x i32> @test_x2sle32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2movf32:
; CHECK: x2movf32 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x2movf32 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <2 x i32> @test_x2movf32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2movt32:
; CHECK: x2movt32 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x2movt32 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <2 x i32> @test_x2movt32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x4seq16:
; CHECK: x4seq16 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x4seq16 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <4 x i16> @test_x4seq16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4slt16:
; CHECK: x4slt16 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x4slt16 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <4 x i16> @test_x4slt16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4sle16:
; CHECK: x4sle16 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x4sle16 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <4 x i16> @test_x4sle16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4movf16:
; CHECK: x4movf16 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x4movf16 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
define <4 x i16> @test_x4movf16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4movt16:
; CHECK: x4movt16 {{[^,]+}}, {{[^,\n}]+}}
; CHECK-NOT: x4movt16 {{[^,]+}}, {{[^,]+}}, {{[^,\n}]+}}
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
