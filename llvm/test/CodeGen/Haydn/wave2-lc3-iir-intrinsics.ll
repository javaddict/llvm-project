; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: Wave 2 intrinsics for LC3 BASOP and IIR biquad operations.
;
; Tests that all 10 Wave 2 intrinsics lower to the correct Haydn instructions:
; LC3 BASOP 16x16: fmul16_hs00, fmulaa16_hs_11_00, fmulss16_hs_11_00
; IIR biquad dual MAC: f2mulaa32rs_hhll/hllh, f2mulss32rs_hhll/hllh
; Shift with rounding: srai64r
; Complex multiply: x4fcmul16rs, x4fcmula16rs
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;LC3 BASOP 16x16 fractional multiply

; CHECK-LABEL: test_fmul16_hs00:
; CHECK: fmul16_hs00
define i64 @test_fmul16_hs00(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <4 x i16>
  %bc.2 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs00(<4 x i16> %bc.1, <4 x i16> %bc.2)
  ret i64 %r
}

; CHECK-LABEL: test_fmulaa16_hs_11_00:
; CHECK: fmulaa16_hs_11_00
define i64 @test_fmulaa16_hs_11_00(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <4 x i16>
  %bc.4 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %acc, <4 x i16> %bc.3, <4 x i16> %bc.4)
  ret i64 %r
}

; CHECK-LABEL: test_fmulss16_hs_11_00:
; CHECK: fmulss16_hs_11_00
define i64 @test_fmulss16_hs_11_00(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <4 x i16>
  %bc.6 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.hs.11.00(i64 %acc, <4 x i16> %bc.5, <4 x i16> %bc.6)
  ret i64 %r
}

;IIR biquad fused dual MAC

; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK: f2mulaa32rs_hhll
define i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

; CHECK-LABEL: test_f2mulaa32rs_hllh:
; CHECK: f2mulaa32rs_hllh
define i64 @test_f2mulaa32rs_hllh(i64 %acc, i64 %a, i64 %b) {
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hllh(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

; CHECK-LABEL: test_f2mulss32rs_hhll:
; CHECK: f2mulss32rs_hhll
define i64 @test_f2mulss32rs_hhll(i64 %acc, i64 %a, i64 %b) {
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulss32rs.hhll(i64 %acc, <2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}

; CHECK-LABEL: test_f2mulss32rs_hllh:
; CHECK: f2mulss32rs_hllh
define i64 @test_f2mulss32rs_hllh(i64 %acc, i64 %a, i64 %b) {
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulss32rs.hllh(i64 %acc, <2 x i32> %bc.13, <2 x i32> %bc.14)
  ret i64 %r
}

;Shift with rounding

; CHECK-LABEL: test_srai64r:
; CHECK: srai64r
define i64 @test_srai64r(i64 %a) {
  %r = call i64 @llvm.haydn.srai64r(i64 %a, i32 8)
  ret i64 %r
}

;Complex multiply

; CHECK-LABEL: test_x4fcmul16rs:
; CHECK: x4fcmul16rs
define i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
  %bc.15 = bitcast i64 %a to <4 x i16>
  %bc.16 = bitcast i64 %b to <4 x i16>
  %call.17 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.15, <4 x i16> %bc.16)
  %r = bitcast <4 x i16> %call.17 to i64
  ret i64 %r
}

; CHECK-LABEL: test_x4fcmula16rs:
; CHECK: x4fcmula16rs
; Ternary accumulator form (acc, a, b) — reads rtd per DB.
define i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
  %bc.18 = bitcast i64 %acc to <4 x i16>
  %bc.19 = bitcast i64 %a to <4 x i16>
  %bc.20 = bitcast i64 %b to <4 x i16>
  %call.21 = call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %bc.18, <4 x i16> %bc.19, <4 x i16> %bc.20)
  %r = bitcast <4 x i16> %call.21 to i64
  ret i64 %r
}

;=== Intrinsics declarations ===

declare i64 @llvm.haydn.fmul16.hs00(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.hs.11.00(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.hs.11.00(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulss32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulss32rs.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.srai64r(i64, i32)
declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16>, <4 x i16>, <4 x i16>)