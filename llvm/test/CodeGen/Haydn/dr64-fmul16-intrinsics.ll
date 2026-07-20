; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
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

declare i64 @llvm.haydn.fmul16.hs01(i64, i64)
declare i64 @llvm.haydn.fmul16.hs02(i64, i64)
declare i64 @llvm.haydn.fmul16.hs03(i64, i64)
declare i64 @llvm.haydn.fmul16.hs11(i64, i64)
declare i64 @llvm.haydn.fmul16.hs12(i64, i64)
declare i64 @llvm.haydn.fmul16.hs13(i64, i64)
declare i64 @llvm.haydn.fmul16.hs22(i64, i64)
declare i64 @llvm.haydn.fmul16.hs23(i64, i64)
declare i64 @llvm.haydn.fmul16.hs33(i64, i64)

define i64 @test_fmul16_hs01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs01:
; CHECK: fmul16_hs01
  %r = call i64 @llvm.haydn.fmul16.hs01(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs02:
; CHECK: fmul16_hs02
  %r = call i64 @llvm.haydn.fmul16.hs02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs03:
; CHECK: fmul16_hs03
  %r = call i64 @llvm.haydn.fmul16.hs03(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs11:
; CHECK: fmul16_hs11
  %r = call i64 @llvm.haydn.fmul16.hs11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs12:
; CHECK: fmul16_hs12
  %r = call i64 @llvm.haydn.fmul16.hs12(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs13:
; CHECK: fmul16_hs13
  %r = call i64 @llvm.haydn.fmul16.hs13(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs22:
; CHECK: fmul16_hs22
  %r = call i64 @llvm.haydn.fmul16.hs22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs23:
; CHECK: fmul16_hs23
  %r = call i64 @llvm.haydn.fmul16.hs23(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_hs33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_hs33:
; CHECK: fmul16_hs33
  %r = call i64 @llvm.haydn.fmul16.hs33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMUL16 LS lane variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul16.ls00(i64, i64)
declare i64 @llvm.haydn.fmul16.ls01(i64, i64)
declare i64 @llvm.haydn.fmul16.ls02(i64, i64)
declare i64 @llvm.haydn.fmul16.ls03(i64, i64)
declare i64 @llvm.haydn.fmul16.ls11(i64, i64)
declare i64 @llvm.haydn.fmul16.ls12(i64, i64)
declare i64 @llvm.haydn.fmul16.ls13(i64, i64)
declare i64 @llvm.haydn.fmul16.ls22(i64, i64)
declare i64 @llvm.haydn.fmul16.ls23(i64, i64)
declare i64 @llvm.haydn.fmul16.ls33(i64, i64)

define i64 @test_fmul16_ls00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls00:
; CHECK: fmul16_ls00
  %r = call i64 @llvm.haydn.fmul16.ls00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls01(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls01:
; CHECK: fmul16_ls01
  %r = call i64 @llvm.haydn.fmul16.ls01(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls02:
; CHECK: fmul16_ls02
  %r = call i64 @llvm.haydn.fmul16.ls02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls03(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls03:
; CHECK: fmul16_ls03
  %r = call i64 @llvm.haydn.fmul16.ls03(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls11(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls11:
; CHECK: fmul16_ls11
  %r = call i64 @llvm.haydn.fmul16.ls11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls12(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls12:
; CHECK: fmul16_ls12
  %r = call i64 @llvm.haydn.fmul16.ls12(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls13(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls13:
; CHECK: fmul16_ls13
  %r = call i64 @llvm.haydn.fmul16.ls13(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls22:
; CHECK: fmul16_ls22
  %r = call i64 @llvm.haydn.fmul16.ls22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls23(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls23:
; CHECK: fmul16_ls23
  %r = call i64 @llvm.haydn.fmul16.ls23(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul16_ls33(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul16_ls33:
; CHECK: fmul16_ls33
  %r = call i64 @llvm.haydn.fmul16.ls33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULAA16 HS/LS MAC lane-pair variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmulaa16.hs.13.02(i64, i64)
declare i64 @llvm.haydn.fmulaa16.hs.33.22(i64, i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.11.00(i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.13.02(i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.33.22(i64, i64)

define i64 @test_fmulaa16_hs_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_hs_13_02:
; CHECK: fmulaa16_hs_13_02
  %r = call i64 @llvm.haydn.fmulaa16.hs.13.02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulaa16_hs_33_22(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_hs_33_22:
; CHECK: fmulaa16_hs_33_22
  %r = call i64 @llvm.haydn.fmulaa16.hs.33.22(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_11_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_11_00:
; CHECK: fmulaa16_ls_11_00
  %r = call i64 @llvm.haydn.fmulaa16.ls.11.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_13_02:
; CHECK: fmulaa16_ls_13_02
  %r = call i64 @llvm.haydn.fmulaa16.ls.13.02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulaa16_ls_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulaa16_ls_33_22:
; CHECK: fmulaa16_ls_33_22
  %r = call i64 @llvm.haydn.fmulaa16.ls.33.22(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULSS16 HS/LS MSU lane-pair variants (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmulss16.hs.13.02(i64, i64)
declare i64 @llvm.haydn.fmulss16.hs.33.22(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.11.00(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.13.02(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.33.22(i64, i64)

define i64 @test_fmulss16_hs_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_hs_13_02:
; CHECK: fmulss16_hs_13_02
  %r = call i64 @llvm.haydn.fmulss16.hs.13.02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulss16_hs_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_hs_33_22:
; CHECK: fmulss16_hs_33_22
  %r = call i64 @llvm.haydn.fmulss16.hs.33.22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulss16_ls_11_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_11_00:
; CHECK: fmulss16_ls_11_00
  %r = call i64 @llvm.haydn.fmulss16.ls.11.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulss16_ls_13_02(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_13_02:
; CHECK: fmulss16_ls_13_02
  %r = call i64 @llvm.haydn.fmulss16.ls.13.02(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulss16_ls_33_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulss16_ls_33_22:
; CHECK: fmulss16_ls_33_22
  %r = call i64 @llvm.haydn.fmulss16.ls.33.22(i64 %a, i64 %b)
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
