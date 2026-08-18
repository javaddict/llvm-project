; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — Comprehensive FMUL16 HS/LS lane variant intrinsics test for Haydn backend.

; Comprehensive FMUL16 HS/LS lane variant intrinsics test for Haydn backend.
; Tests all fractional 16-bit multiply, MAC, and MSU intrinsics with lane selection.
; These are critical for LC3/BASOP audio codec kernels.
;
; Categories:
; FMUL16 HS remaining lane variants (01-33, excluding 00)
; FMUL16 LS all lane variants (00-33)
; FMULAA16 HS/LS MAC lane-pair variants
; FMULSS16 HS/LS MSU lane-pair variants
; FMULS16 HS/LS subtract variants (all lanes)

;===----------------------------------------------------------------------===;
; FMUL16 HS lane variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul16.hs01(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs03(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs11(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs12(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs13(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs22(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs23(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs33(<4 x i16>, <4 x i16>)
define i64 @test_fmul16_hs01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs01:
; CHECK: fmul16_hs01
  %bc.1 = bitcast i64 %a to <4 x i16>
  %bc.2 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs01(<4 x i16> %bc.1, <4 x i16> %bc.2)
  ret i64 %r
}

define i64 @test_fmul16_hs02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs02:
; CHECK: fmul16_hs02
  %bc.3 = bitcast i64 %a to <4 x i16>
  %bc.4 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs02(<4 x i16> %bc.3, <4 x i16> %bc.4)
  ret i64 %r
}

define i64 @test_fmul16_hs03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs03:
; CHECK: fmul16_hs03
  %bc.5 = bitcast i64 %a to <4 x i16>
  %bc.6 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs03(<4 x i16> %bc.5, <4 x i16> %bc.6)
  ret i64 %r
}

define i64 @test_fmul16_hs11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs11:
; CHECK: fmul16_hs11
  %bc.7 = bitcast i64 %a to <4 x i16>
  %bc.8 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs11(<4 x i16> %bc.7, <4 x i16> %bc.8)
  ret i64 %r
}

define i64 @test_fmul16_hs12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs12:
; CHECK: fmul16_hs12
  %bc.9 = bitcast i64 %a to <4 x i16>
  %bc.10 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs12(<4 x i16> %bc.9, <4 x i16> %bc.10)
  ret i64 %r
}

define i64 @test_fmul16_hs13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs13:
; CHECK: fmul16_hs13
  %bc.11 = bitcast i64 %a to <4 x i16>
  %bc.12 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs13(<4 x i16> %bc.11, <4 x i16> %bc.12)
  ret i64 %r
}

define i64 @test_fmul16_hs22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs22:
; CHECK: fmul16_hs22
  %bc.13 = bitcast i64 %a to <4 x i16>
  %bc.14 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs22(<4 x i16> %bc.13, <4 x i16> %bc.14)
  ret i64 %r
}

define i64 @test_fmul16_hs23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs23:
; CHECK: fmul16_hs23
  %bc.15 = bitcast i64 %a to <4 x i16>
  %bc.16 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs23(<4 x i16> %bc.15, <4 x i16> %bc.16)
  ret i64 %r
}

define i64 @test_fmul16_hs33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs33:
; CHECK: fmul16_hs33
  %bc.17 = bitcast i64 %a to <4 x i16>
  %bc.18 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs33(<4 x i16> %bc.17, <4 x i16> %bc.18)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMUL16 LS lane variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul16.ls00(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls01(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls03(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls11(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls12(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls13(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls22(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls23(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.ls33(<4 x i16>, <4 x i16>)
define i64 @test_fmul16_ls00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls00:
; CHECK: fmul16_ls00
  %bc.19 = bitcast i64 %a to <4 x i16>
  %bc.20 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls00(<4 x i16> %bc.19, <4 x i16> %bc.20)
  ret i64 %r
}

define i64 @test_fmul16_ls01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls01:
; CHECK: fmul16_ls01
  %bc.21 = bitcast i64 %a to <4 x i16>
  %bc.22 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls01(<4 x i16> %bc.21, <4 x i16> %bc.22)
  ret i64 %r
}

define i64 @test_fmul16_ls02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls02:
; CHECK: fmul16_ls02
  %bc.23 = bitcast i64 %a to <4 x i16>
  %bc.24 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls02(<4 x i16> %bc.23, <4 x i16> %bc.24)
  ret i64 %r
}

define i64 @test_fmul16_ls03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls03:
; CHECK: fmul16_ls03
  %bc.25 = bitcast i64 %a to <4 x i16>
  %bc.26 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls03(<4 x i16> %bc.25, <4 x i16> %bc.26)
  ret i64 %r
}

define i64 @test_fmul16_ls11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls11:
; CHECK: fmul16_ls11
  %bc.27 = bitcast i64 %a to <4 x i16>
  %bc.28 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls11(<4 x i16> %bc.27, <4 x i16> %bc.28)
  ret i64 %r
}

define i64 @test_fmul16_ls12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls12:
; CHECK: fmul16_ls12
  %bc.29 = bitcast i64 %a to <4 x i16>
  %bc.30 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls12(<4 x i16> %bc.29, <4 x i16> %bc.30)
  ret i64 %r
}

define i64 @test_fmul16_ls13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls13:
; CHECK: fmul16_ls13
  %bc.31 = bitcast i64 %a to <4 x i16>
  %bc.32 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls13(<4 x i16> %bc.31, <4 x i16> %bc.32)
  ret i64 %r
}

define i64 @test_fmul16_ls22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls22:
; CHECK: fmul16_ls22
  %bc.33 = bitcast i64 %a to <4 x i16>
  %bc.34 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls22(<4 x i16> %bc.33, <4 x i16> %bc.34)
  ret i64 %r
}

define i64 @test_fmul16_ls23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls23:
; CHECK: fmul16_ls23
  %bc.35 = bitcast i64 %a to <4 x i16>
  %bc.36 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls23(<4 x i16> %bc.35, <4 x i16> %bc.36)
  ret i64 %r
}

define i64 @test_fmul16_ls33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls33:
; CHECK: fmul16_ls33
  %bc.37 = bitcast i64 %a to <4 x i16>
  %bc.38 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls33(<4 x i16> %bc.37, <4 x i16> %bc.38)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULAA16 HS/LS MAC lane-pair variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmulaa16.hs.13.02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.hs.33.22(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.11.00(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.13.02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.33.22(<4 x i16>, <4 x i16>)
define i64 @test_fmulaa16_hs_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_hs_13_02:
; CHECK: fmulaa16_hs_13_02
  %bc.39 = bitcast i64 %a to <4 x i16>
  %bc.40 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.hs.13.02(<4 x i16> %bc.39, <4 x i16> %bc.40)
  ret i64 %r
}

define i64 @test_fmulaa16_hs_33_22(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_hs_33_22:
; CHECK: fmulaa16_hs_33_22
  %bc.41 = bitcast i64 %a to <4 x i16>
  %bc.42 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.hs.33.22(i64 %acc, <4 x i16> %bc.41, <4 x i16> %bc.42)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_11_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_11_00:
; CHECK: fmulaa16_ls_11_00
  %bc.43 = bitcast i64 %a to <4 x i16>
  %bc.44 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.11.00(<4 x i16> %bc.43, <4 x i16> %bc.44)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_13_02:
; CHECK: fmulaa16_ls_13_02
  %bc.45 = bitcast i64 %a to <4 x i16>
  %bc.46 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.13.02(<4 x i16> %bc.45, <4 x i16> %bc.46)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_33_22:
; CHECK: fmulaa16_ls_33_22
  %bc.47 = bitcast i64 %a to <4 x i16>
  %bc.48 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.33.22(<4 x i16> %bc.47, <4 x i16> %bc.48)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULSS16 HS/LS MSU lane-pair variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmulss16.hs.13.02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.hs.33.22(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.11.00(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.13.02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.33.22(<4 x i16>, <4 x i16>)
define i64 @test_fmulss16_hs_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_hs_13_02:
; CHECK: fmulss16_hs_13_02
  %bc.49 = bitcast i64 %a to <4 x i16>
  %bc.50 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.hs.13.02(<4 x i16> %bc.49, <4 x i16> %bc.50)
  ret i64 %r
}

define i64 @test_fmulss16_hs_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_hs_33_22:
; CHECK: fmulss16_hs_33_22
  %bc.51 = bitcast i64 %a to <4 x i16>
  %bc.52 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.hs.33.22(<4 x i16> %bc.51, <4 x i16> %bc.52)
  ret i64 %r
}

define i64 @test_fmulss16_ls_11_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_11_00:
; CHECK: fmulss16_ls_11_00
  %bc.53 = bitcast i64 %a to <4 x i16>
  %bc.54 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.11.00(<4 x i16> %bc.53, <4 x i16> %bc.54)
  ret i64 %r
}

define i64 @test_fmulss16_ls_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_13_02:
; CHECK: fmulss16_ls_13_02
  %bc.55 = bitcast i64 %a to <4 x i16>
  %bc.56 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.13.02(<4 x i16> %bc.55, <4 x i16> %bc.56)
  ret i64 %r
}

define i64 @test_fmulss16_ls_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_33_22:
; CHECK: fmulss16_ls_33_22
  %bc.57 = bitcast i64 %a to <4 x i16>
  %bc.58 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.33.22(<4 x i16> %bc.57, <4 x i16> %bc.58)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULS16 HS subtract lane variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls16.hs00(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs01(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs02(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs03(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs11(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs12(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs13(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs22(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs23(i64, i64)
declare i64 @llvm.haydn.fmuls16.hs33(i64, i64)
define i64 @test_fmuls16_hs00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs00:
; CHECK: fmuls16_hs00
  %r = call i64 @llvm.haydn.fmuls16.hs00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs01:
; CHECK: fmuls16_hs01
  %r = call i64 @llvm.haydn.fmuls16.hs01(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs02:
; CHECK: fmuls16_hs02
  %r = call i64 @llvm.haydn.fmuls16.hs02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs03:
; CHECK: fmuls16_hs03
  %r = call i64 @llvm.haydn.fmuls16.hs03(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs11:
; CHECK: fmuls16_hs11
  %r = call i64 @llvm.haydn.fmuls16.hs11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs12:
; CHECK: fmuls16_hs12
  %r = call i64 @llvm.haydn.fmuls16.hs12(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs13:
; CHECK: fmuls16_hs13
  %r = call i64 @llvm.haydn.fmuls16.hs13(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs22:
; CHECK: fmuls16_hs22
  %r = call i64 @llvm.haydn.fmuls16.hs22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs23:
; CHECK: fmuls16_hs23
  %r = call i64 @llvm.haydn.fmuls16.hs23(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_hs33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_hs33:
; CHECK: fmuls16_hs33
  %r = call i64 @llvm.haydn.fmuls16.hs33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULS16 LS subtract lane variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls16.ls00(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls01(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls02(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls03(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls11(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls12(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls13(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls22(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls23(i64, i64)
declare i64 @llvm.haydn.fmuls16.ls33(i64, i64)
define i64 @test_fmuls16_ls00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls00:
; CHECK: fmuls16_ls00
  %r = call i64 @llvm.haydn.fmuls16.ls00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls01:
; CHECK: fmuls16_ls01
  %r = call i64 @llvm.haydn.fmuls16.ls01(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls02:
; CHECK: fmuls16_ls02
  %r = call i64 @llvm.haydn.fmuls16.ls02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls03:
; CHECK: fmuls16_ls03
  %r = call i64 @llvm.haydn.fmuls16.ls03(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls11:
; CHECK: fmuls16_ls11
  %r = call i64 @llvm.haydn.fmuls16.ls11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls12:
; CHECK: fmuls16_ls12
  %r = call i64 @llvm.haydn.fmuls16.ls12(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls13:
; CHECK: fmuls16_ls13
  %r = call i64 @llvm.haydn.fmuls16.ls13(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls22:
; CHECK: fmuls16_ls22
  %r = call i64 @llvm.haydn.fmuls16.ls22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls23:
; CHECK: fmuls16_ls23
  %r = call i64 @llvm.haydn.fmuls16.ls23(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls16_ls33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls16_ls33:
; CHECK: fmuls16_ls33
  %r = call i64 @llvm.haydn.fmuls16.ls33(i64 %a, i64 %b)
  ret i64 %r
}
