; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Wave 4 Tier 1 intrinsics: X2/X4 fractional multiply, shift-with-rounding
; FMUL16 HS/LS parameterized, F2MUL zero-accumulator variants.
;
; All intrinsics are binary DR64 (i64 in, i64 out) unless noted otherwise.

;===---------------------------------------------------------------------===;
; A. X2 SIMD fractional multiply
;===---------------------------------------------------------------------===;

define dso_local i64 @test_x2fmul32rs(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmul32rs(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32rs:
; CHECK: x2fmul32rs

define dso_local i64 @test_x2fmul32rss(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmul32rss(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32rss:
; CHECK: x2fmul32rss

define dso_local i64 @test_x2fmul32ts(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmul32ts(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32ts:
; CHECK: x2fmul32ts

define dso_local i64 @test_x2fmula32rs(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmula32rs(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32rs:
; CHECK: x2fmula32rs

define dso_local i64 @test_x2fmula32rss(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmula32rss(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32rss:
; CHECK: x2fmula32rss

define dso_local i64 @test_x2fmula32ts(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmula32ts(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32ts:
; CHECK: x2fmula32ts

define dso_local i64 @test_x2fmuls32rs(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmuls32rs(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32rs:
; CHECK: x2fmuls32rs

define dso_local i64 @test_x2fmuls32rss(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmuls32rss(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32rss:
; CHECK: x2fmuls32rss

define dso_local i64 @test_x2fmuls32ts(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x2fmuls32ts(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32ts:
; CHECK: x2fmuls32ts

;===---------------------------------------------------------------------===;
; B. X4 SIMD fractional multiply
;===---------------------------------------------------------------------===;

define dso_local i64 @test_x4fmul16rs(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x4fmul16rs(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x4fmul16rs:
; CHECK: x4fmul16rs

define dso_local i64 @test_x4fmul16rss(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x4fmul16rss(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x4fmul16rss:
; CHECK: x4fmul16rss

define dso_local i64 @test_x4fmul16ts(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x4fmul16ts(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x4fmul16ts:
; CHECK: x4fmul16ts

;===---------------------------------------------------------------------===;
; C. X2/X4 shift with rounding
;===---------------------------------------------------------------------===;

define dso_local <2 x i32> @test_x2frsst32(<2 x i32> %a, <2 x i32> %b) {
entry:
  %r = call <2 x i32> @llvm.haydn.x2frsst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}
; CHECK-LABEL: test_x2frsst32:
; CHECK: x2frsst32

define dso_local <2 x i32> @test_x2frst32(<2 x i32> %a, <2 x i32> %b) {
entry:
  %r = call <2 x i32> @llvm.haydn.x2frst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}
; CHECK-LABEL: test_x2frst32:
; CHECK: x2frst32

define dso_local i64 @test_x4frsst16(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x4frsst16(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x4frsst16:
; CHECK: x4frsst16

define dso_local i64 @test_x4frst16(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.x4frst16(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_x4frst16:
; CHECK: x4frst16

;===---------------------------------------------------------------------===;
; D. X2SRAI32R / X4SRAI16R — immediate shift with rounding
;===---------------------------------------------------------------------===;

define dso_local <2 x i32> @test_x2srai32r(<2 x i32> %a) {
entry:
  %r = call <2 x i32> @llvm.haydn.x2srai32r(<2 x i32> %a, i32 5)
  ret <2 x i32> %r
}
; CHECK-LABEL: test_x2srai32r:
; CHECK: x2srai32r

define dso_local <4 x i16> @test_x4srai16r(<4 x i16> %a) {
entry:
  %r = call <4 x i16> @llvm.haydn.x4srai16r(<4 x i16> %a, i32 3)
  ret <4 x i16> %r
}
; CHECK-LABEL: test_x4srai16r:
; CHECK: x4srai16r

;===---------------------------------------------------------------------===;
; E. FMUL16_HS remaining variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmul16_hs01(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs01(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs01:
; CHECK: fmul16_hs01

define dso_local i64 @test_fmul16_hs02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs02:
; CHECK: fmul16_hs02

define dso_local i64 @test_fmul16_hs03(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs03(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs03:
; CHECK: fmul16_hs03

define dso_local i64 @test_fmul16_hs11(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs11(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs11:
; CHECK: fmul16_hs11

define dso_local i64 @test_fmul16_hs12(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs12(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs12:
; CHECK: fmul16_hs12

define dso_local i64 @test_fmul16_hs13(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs13(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs13:
; CHECK: fmul16_hs13

define dso_local i64 @test_fmul16_hs22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs22:
; CHECK: fmul16_hs22

define dso_local i64 @test_fmul16_hs23(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs23(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs23:
; CHECK: fmul16_hs23

define dso_local i64 @test_fmul16_hs33(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.hs33(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs33:
; CHECK: fmul16_hs33

;===---------------------------------------------------------------------===;
; F. FMUL16_LS variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmul16_ls00(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls00(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls00:
; CHECK: fmul16_ls00

define dso_local i64 @test_fmul16_ls01(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls01(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls01:
; CHECK: fmul16_ls01

define dso_local i64 @test_fmul16_ls02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls02:
; CHECK: fmul16_ls02

define dso_local i64 @test_fmul16_ls03(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls03(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls03:
; CHECK: fmul16_ls03

define dso_local i64 @test_fmul16_ls11(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls11(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls11:
; CHECK: fmul16_ls11

define dso_local i64 @test_fmul16_ls12(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls12(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls12:
; CHECK: fmul16_ls12

define dso_local i64 @test_fmul16_ls13(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls13(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls13:
; CHECK: fmul16_ls13

define dso_local i64 @test_fmul16_ls22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls22:
; CHECK: fmul16_ls22

define dso_local i64 @test_fmul16_ls23(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls23(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls23:
; CHECK: fmul16_ls23

define dso_local i64 @test_fmul16_ls33(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmul16.ls33(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls33:
; CHECK: fmul16_ls33

;===---------------------------------------------------------------------===;
; G. FMULAA16 HS/LS MAC variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmulaa16_hs_13_02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulaa16.hs.13.02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_hs_13_02:
; CHECK: fmulaa16_hs_13_02

define dso_local i64 @test_fmulaa16_hs_33_22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulaa16.hs.33.22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_hs_33_22:
; CHECK: fmulaa16_hs_33_22

define dso_local i64 @test_fmulaa16_ls_11_00(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulaa16.ls.11.00(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_11_00:
; CHECK: fmulaa16_ls_11_00

define dso_local i64 @test_fmulaa16_ls_13_02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulaa16.ls.13.02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_13_02:
; CHECK: fmulaa16_ls_13_02

define dso_local i64 @test_fmulaa16_ls_33_22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulaa16.ls.33.22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_33_22:
; CHECK: fmulaa16_ls_33_22

;===---------------------------------------------------------------------===;
; H. FMULSS16 HS/LS MSU variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmulss16_hs_13_02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulss16.hs.13.02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_hs_13_02:
; CHECK: fmulss16_hs_13_02

define dso_local i64 @test_fmulss16_hs_33_22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulss16.hs.33.22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_hs_33_22:
; CHECK: fmulss16_hs_33_22

define dso_local i64 @test_fmulss16_ls_11_00(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulss16.ls.11.00(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_11_00:
; CHECK: fmulss16_ls_11_00

define dso_local i64 @test_fmulss16_ls_13_02(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulss16.ls.13.02(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_13_02:
; CHECK: fmulss16_ls_13_02

define dso_local i64 @test_fmulss16_ls_33_22(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.fmulss16.ls.33.22(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_33_22:
; CHECK: fmulss16_ls_33_22

;===---------------------------------------------------------------------===;
; I. F2MUL zero-accumulator variants (saturating + rounding)
;===---------------------------------------------------------------------===;

define dso_local i64 @test_f2mulas32rs_hhll(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulas32rs.hhll(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32rs_hhll:
; CHECK: f2mulas32rs_hhll

define dso_local i64 @test_f2mulas32rs_hllh(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulas32rs.hllh(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32rs_hllh:
; CHECK: f2mulas32rs_hllh

define dso_local i64 @test_f2mulsa32rs_hhll(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulsa32rs.hhll(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32rs_hhll:
; CHECK: f2mulsa32rs_hhll

define dso_local i64 @test_f2mulsa32rs_hllh(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulsa32rs.hllh(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32rs_hllh:
; CHECK: f2mulsa32rs_hllh

;===---------------------------------------------------------------------===;
; J. F2MUL zero-accumulator variants (non-saturating + rounding)
;===---------------------------------------------------------------------===;

define dso_local i64 @test_f2mulas32r_hhll(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulas32r.hhll(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32r_hhll:
; CHECK: f2mulas32r_hhll

define dso_local i64 @test_f2mulas32r_hllh(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulas32r.hllh(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32r_hllh:
; CHECK: f2mulas32r_hllh

define dso_local i64 @test_f2mulsa32r_hhll(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulsa32r.hhll(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32r_hhll:
; CHECK: f2mulsa32r_hhll

define dso_local i64 @test_f2mulsa32r_hllh(i64 %a, i64 %b) {
entry:
  %r = call i64 @llvm.haydn.f2mulsa32r.hllh(i64 %a, i64 %b)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32r_hllh:
; CHECK: f2mulsa32r_hllh

;===---------------------------------------------------------------------===;
; Intrinsic declarations
;===---------------------------------------------------------------------===;

; X2 SIMD fractional multiply
declare i64 @llvm.haydn.x2fmul32rs(i64, i64)
declare i64 @llvm.haydn.x2fmul32rss(i64, i64)
declare i64 @llvm.haydn.x2fmul32ts(i64, i64)
declare i64 @llvm.haydn.x2fmula32rs(i64, i64)
declare i64 @llvm.haydn.x2fmula32rss(i64, i64)
declare i64 @llvm.haydn.x2fmula32ts(i64, i64)
declare i64 @llvm.haydn.x2fmuls32rs(i64, i64)
declare i64 @llvm.haydn.x2fmuls32rss(i64, i64)
declare i64 @llvm.haydn.x2fmuls32ts(i64, i64)

; X4 SIMD fractional multiply
declare i64 @llvm.haydn.x4fmul16rs(i64, i64)
declare i64 @llvm.haydn.x4fmul16rss(i64, i64)
declare i64 @llvm.haydn.x4fmul16ts(i64, i64)

; X2/X4 shift with rounding
declare <2 x i32> @llvm.haydn.x2frsst32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2frst32(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.x4frsst16(i64, i64)
declare i64 @llvm.haydn.x4frst16(i64, i64)

; X2SRAI32R / X4SRAI16R
declare <2 x i32> @llvm.haydn.x2srai32r(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4srai16r(<4 x i16>, i32)

; FMUL16_HS remaining
declare i64 @llvm.haydn.fmul16.hs01(i64, i64)
declare i64 @llvm.haydn.fmul16.hs02(i64, i64)
declare i64 @llvm.haydn.fmul16.hs03(i64, i64)
declare i64 @llvm.haydn.fmul16.hs11(i64, i64)
declare i64 @llvm.haydn.fmul16.hs12(i64, i64)
declare i64 @llvm.haydn.fmul16.hs13(i64, i64)
declare i64 @llvm.haydn.fmul16.hs22(i64, i64)
declare i64 @llvm.haydn.fmul16.hs23(i64, i64)
declare i64 @llvm.haydn.fmul16.hs33(i64, i64)

; FMUL16_LS
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

; FMULAA16 HS/LS MAC
declare i64 @llvm.haydn.fmulaa16.hs.13.02(i64, i64)
declare i64 @llvm.haydn.fmulaa16.hs.33.22(i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.11.00(i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.13.02(i64, i64)
declare i64 @llvm.haydn.fmulaa16.ls.33.22(i64, i64)

; FMULSS16 HS/LS MSU
declare i64 @llvm.haydn.fmulss16.hs.13.02(i64, i64)
declare i64 @llvm.haydn.fmulss16.hs.33.22(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.11.00(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.13.02(i64, i64)
declare i64 @llvm.haydn.fmulss16.ls.33.22(i64, i64)

; F2MUL zero-accumulator (saturating + rounding)
declare i64 @llvm.haydn.f2mulas32rs.hhll(i64, i64)
declare i64 @llvm.haydn.f2mulas32rs.hllh(i64, i64)
declare i64 @llvm.haydn.f2mulsa32rs.hhll(i64, i64)
declare i64 @llvm.haydn.f2mulsa32rs.hllh(i64, i64)

; F2MUL zero-accumulator (non-saturating + rounding)
declare i64 @llvm.haydn.f2mulas32r.hhll(i64, i64)
declare i64 @llvm.haydn.f2mulas32r.hllh(i64, i64)
declare i64 @llvm.haydn.f2mulsa32r.hhll(i64, i64)
declare i64 @llvm.haydn.f2mulsa32r.hllh(i64, i64)
