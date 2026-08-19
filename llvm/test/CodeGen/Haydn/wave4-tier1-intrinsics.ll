; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Wave 4 Tier 1 intrinsics: X2/X4 fractional multiply, shift-with-rounding FMUL16 HS/LS parameterized, F2MUL zero-accumulator variants.

; Wave 4 Tier 1 intrinsics: X2/X4 fractional multiply, shift-with-rounding
; FMUL16 HS/LS parameterized, F2MUL zero-accumulator variants.
;
; All intrinsics are binary DR64 (i64 in, i64 out) unless noted otherwise.

;===---------------------------------------------------------------------===;
; A. X2 SIMD fractional multiply
;===---------------------------------------------------------------------===;

define dso_local i64 @test_x2fmul32rs(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2fmul32rs(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32rs:
; CHECK: x2fmul32rs

define dso_local i64 @test_x2fmul32rss(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2fmul32rss(<2 x i32> %bc.4, <2 x i32> %bc.5)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32rss:
; CHECK: x2fmul32rss

define dso_local i64 @test_x2fmul32ts(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %call.9 = call <2 x i32> @llvm.haydn.x2fmul32ts(<2 x i32> %bc.7, <2 x i32> %bc.8)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmul32ts:
; CHECK: x2fmul32ts

define dso_local i64 @test_x2fmula32rs(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2fmula32rs(<2 x i32> %bc.1, <2 x i32> %bc.2, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32rs:
; CHECK: x2fmula32rs

define dso_local i64 @test_x2fmula32rss(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2fmula32rss(<2 x i32> %bc.4, <2 x i32> %bc.5, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32rss:
; CHECK: x2fmula32rss

define dso_local i64 @test_x2fmula32ts(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %call.9 = call <2 x i32> @llvm.haydn.x2fmula32ts(<2 x i32> %bc.7, <2 x i32> %bc.8, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmula32ts:
; CHECK: x2fmula32ts

define dso_local i64 @test_x2fmuls32rs(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.10 = bitcast i64 %a to <2 x i32>
  %bc.11 = bitcast i64 %b to <2 x i32>
  %call.12 = call <2 x i32> @llvm.haydn.x2fmuls32rs(<2 x i32> %bc.10, <2 x i32> %bc.11, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.12 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32rs:
; CHECK: x2fmuls32rs

define dso_local i64 @test_x2fmuls32rss(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %call.15 = call <2 x i32> @llvm.haydn.x2fmuls32rss(<2 x i32> %bc.13, <2 x i32> %bc.14, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.15 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32rss:
; CHECK: x2fmuls32rss

define dso_local i64 @test_x2fmuls32ts(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.16 = bitcast i64 %a to <2 x i32>
  %bc.17 = bitcast i64 %b to <2 x i32>
  %call.18 = call <2 x i32> @llvm.haydn.x2fmuls32ts(<2 x i32> %bc.16, <2 x i32> %bc.17, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.18 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x2fmuls32ts:
; CHECK: x2fmuls32ts

;===---------------------------------------------------------------------===;
; B. X4 SIMD fractional multiply
;===---------------------------------------------------------------------===;

define dso_local i64 @test_x4fmul16rs(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.10 = bitcast i64 %a to <4 x i16>
  %bc.11 = bitcast i64 %b to <4 x i16>
  %call.12 = call <4 x i16> @llvm.haydn.x4fmul16rs(<4 x i16> %bc.10, <4 x i16> %bc.11)
  %r = bitcast <4 x i16> %call.12 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x4fmul16rs:
; CHECK: x4fmul16rs

define dso_local i64 @test_x4fmul16rss(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.13 = bitcast i64 %a to <4 x i16>
  %bc.14 = bitcast i64 %b to <4 x i16>
  %call.15 = call <4 x i16> @llvm.haydn.x4fmul16rss(<4 x i16> %bc.13, <4 x i16> %bc.14)
  %r = bitcast <4 x i16> %call.15 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x4fmul16rss:
; CHECK: x4fmul16rss

define dso_local i64 @test_x4fmul16ts(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.16 = bitcast i64 %a to <4 x i16>
  %bc.17 = bitcast i64 %b to <4 x i16>
  %call.18 = call <4 x i16> @llvm.haydn.x4fmul16ts(<4 x i16> %bc.16, <4 x i16> %bc.17)
  %r = bitcast <4 x i16> %call.18 to i64
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

define dso_local i64 @test_x4frsst16(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.19 = bitcast i64 %a to <4 x i16>
  %bc.20 = bitcast i64 %b to <4 x i16>
  %call.21 = call <4 x i16> @llvm.haydn.x4frsst16(<4 x i16> %bc.19, <4 x i16> %bc.20)
  %r = bitcast <4 x i16> %call.21 to i64
  ret i64 %r
}
; CHECK-LABEL: test_x4frsst16:
; CHECK: x4frsst16

define dso_local i64 @test_x4frst16(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.22 = bitcast i64 %a to <4 x i16>
  %bc.23 = bitcast i64 %b to <4 x i16>
  %call.24 = call <4 x i16> @llvm.haydn.x4frst16(<4 x i16> %bc.22, <4 x i16> %bc.23)
  %r = bitcast <4 x i16> %call.24 to i64
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

define dso_local i64 @test_fmul16_hs01(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.25 = bitcast i64 %a to <4 x i16>
  %bc.26 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs01(<4 x i16> %bc.25, <4 x i16> %bc.26)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs01:
; CHECK: fmul16_hs01

define dso_local i64 @test_fmul16_hs02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.27 = bitcast i64 %a to <4 x i16>
  %bc.28 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs02(<4 x i16> %bc.27, <4 x i16> %bc.28)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs02:
; CHECK: fmul16_hs02

define dso_local i64 @test_fmul16_hs03(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.29 = bitcast i64 %a to <4 x i16>
  %bc.30 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs03(<4 x i16> %bc.29, <4 x i16> %bc.30)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs03:
; CHECK: fmul16_hs03

define dso_local i64 @test_fmul16_hs11(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.31 = bitcast i64 %a to <4 x i16>
  %bc.32 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs11(<4 x i16> %bc.31, <4 x i16> %bc.32)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs11:
; CHECK: fmul16_hs11

define dso_local i64 @test_fmul16_hs12(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.33 = bitcast i64 %a to <4 x i16>
  %bc.34 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs12(<4 x i16> %bc.33, <4 x i16> %bc.34)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs12:
; CHECK: fmul16_hs12

define dso_local i64 @test_fmul16_hs13(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.35 = bitcast i64 %a to <4 x i16>
  %bc.36 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs13(<4 x i16> %bc.35, <4 x i16> %bc.36)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs13:
; CHECK: fmul16_hs13

define dso_local i64 @test_fmul16_hs22(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.37 = bitcast i64 %a to <4 x i16>
  %bc.38 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs22(<4 x i16> %bc.37, <4 x i16> %bc.38)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs22:
; CHECK: fmul16_hs22

define dso_local i64 @test_fmul16_hs23(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.39 = bitcast i64 %a to <4 x i16>
  %bc.40 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs23(<4 x i16> %bc.39, <4 x i16> %bc.40)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs23:
; CHECK: fmul16_hs23

define dso_local i64 @test_fmul16_hs33(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.41 = bitcast i64 %a to <4 x i16>
  %bc.42 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.hs33(<4 x i16> %bc.41, <4 x i16> %bc.42)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_hs33:
; CHECK: fmul16_hs33

;===---------------------------------------------------------------------===;
; F. FMUL16_LS variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmul16_ls00(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.43 = bitcast i64 %a to <4 x i16>
  %bc.44 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls00(<4 x i16> %bc.43, <4 x i16> %bc.44)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls00:
; CHECK: fmul16_ls00

define dso_local i64 @test_fmul16_ls01(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.45 = bitcast i64 %a to <4 x i16>
  %bc.46 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls01(<4 x i16> %bc.45, <4 x i16> %bc.46)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls01:
; CHECK: fmul16_ls01

define dso_local i64 @test_fmul16_ls02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.47 = bitcast i64 %a to <4 x i16>
  %bc.48 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls02(<4 x i16> %bc.47, <4 x i16> %bc.48)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls02:
; CHECK: fmul16_ls02

define dso_local i64 @test_fmul16_ls03(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.49 = bitcast i64 %a to <4 x i16>
  %bc.50 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls03(<4 x i16> %bc.49, <4 x i16> %bc.50)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls03:
; CHECK: fmul16_ls03

define dso_local i64 @test_fmul16_ls11(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.51 = bitcast i64 %a to <4 x i16>
  %bc.52 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls11(<4 x i16> %bc.51, <4 x i16> %bc.52)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls11:
; CHECK: fmul16_ls11

define dso_local i64 @test_fmul16_ls12(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.53 = bitcast i64 %a to <4 x i16>
  %bc.54 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls12(<4 x i16> %bc.53, <4 x i16> %bc.54)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls12:
; CHECK: fmul16_ls12

define dso_local i64 @test_fmul16_ls13(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.55 = bitcast i64 %a to <4 x i16>
  %bc.56 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls13(<4 x i16> %bc.55, <4 x i16> %bc.56)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls13:
; CHECK: fmul16_ls13

define dso_local i64 @test_fmul16_ls22(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.57 = bitcast i64 %a to <4 x i16>
  %bc.58 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls22(<4 x i16> %bc.57, <4 x i16> %bc.58)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls22:
; CHECK: fmul16_ls22

define dso_local i64 @test_fmul16_ls23(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.59 = bitcast i64 %a to <4 x i16>
  %bc.60 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls23(<4 x i16> %bc.59, <4 x i16> %bc.60)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls23:
; CHECK: fmul16_ls23

define dso_local i64 @test_fmul16_ls33(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.61 = bitcast i64 %a to <4 x i16>
  %bc.62 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmul16.ls33(<4 x i16> %bc.61, <4 x i16> %bc.62)
  ret i64 %r
}
; CHECK-LABEL: test_fmul16_ls33:
; CHECK: fmul16_ls33

;===---------------------------------------------------------------------===;
; G. FMULAA16 HS/LS MAC variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmulaa16_hs_13_02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.63 = bitcast i64 %a to <4 x i16>
  %bc.64 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.hs.13.02(i64 %c, <4 x i16> %bc.63, <4 x i16> %bc.64)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_hs_13_02:
; CHECK: fmulaa16_hs_13_02

define dso_local i64 @test_fmulaa16_hs_33_22(i64 %acc, i64 %a, i64 %b) {
entry:
  %bc.65 = bitcast i64 %a to <4 x i16>
  %bc.66 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.hs.33.22(i64 %acc, <4 x i16> %bc.65, <4 x i16> %bc.66)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_hs_33_22:
; CHECK: fmulaa16_hs_33_22

define dso_local i64 @test_fmulaa16_ls_11_00(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.67 = bitcast i64 %a to <4 x i16>
  %bc.68 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.11.00(i64 %c, <4 x i16> %bc.67, <4 x i16> %bc.68)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_11_00:
; CHECK: fmulaa16_ls_11_00

define dso_local i64 @test_fmulaa16_ls_13_02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.69 = bitcast i64 %a to <4 x i16>
  %bc.70 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.13.02(i64 %c, <4 x i16> %bc.69, <4 x i16> %bc.70)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_13_02:
; CHECK: fmulaa16_ls_13_02

define dso_local i64 @test_fmulaa16_ls_33_22(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.71 = bitcast i64 %a to <4 x i16>
  %bc.72 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulaa16.ls.33.22(i64 %c, <4 x i16> %bc.71, <4 x i16> %bc.72)
  ret i64 %r
}
; CHECK-LABEL: test_fmulaa16_ls_33_22:
; CHECK: fmulaa16_ls_33_22

;===---------------------------------------------------------------------===;
; H. FMULSS16 HS/LS MSU variants
;===---------------------------------------------------------------------===;

define dso_local i64 @test_fmulss16_hs_13_02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.73 = bitcast i64 %a to <4 x i16>
  %bc.74 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.hs.13.02(i64 %c, <4 x i16> %bc.73, <4 x i16> %bc.73)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_hs_13_02:
; CHECK: fmulss16_hs_13_02

define dso_local i64 @test_fmulss16_hs_33_22(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.75 = bitcast i64 %a to <4 x i16>
  %bc.76 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.hs.33.22(i64 %c, <4 x i16> %bc.75, <4 x i16> %bc.75)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_hs_33_22:
; CHECK: fmulss16_hs_33_22

define dso_local i64 @test_fmulss16_ls_11_00(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.77 = bitcast i64 %a to <4 x i16>
  %bc.78 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.11.00(i64 %c, <4 x i16> %bc.77, <4 x i16> %bc.77)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_11_00:
; CHECK: fmulss16_ls_11_00

define dso_local i64 @test_fmulss16_ls_13_02(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.79 = bitcast i64 %a to <4 x i16>
  %bc.80 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.13.02(i64 %c, <4 x i16> %bc.79, <4 x i16> %bc.79)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_13_02:
; CHECK: fmulss16_ls_13_02

define dso_local i64 @test_fmulss16_ls_33_22(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.81 = bitcast i64 %a to <4 x i16>
  %bc.82 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.fmulss16.ls.33.22(i64 %c, <4 x i16> %bc.81, <4 x i16> %bc.81)
  ret i64 %r
}
; CHECK-LABEL: test_fmulss16_ls_33_22:
; CHECK: fmulss16_ls_33_22

;===---------------------------------------------------------------------===;
; I. F2MUL zero-accumulator variants (saturating + rounding)
;===---------------------------------------------------------------------===;

define dso_local i64 @test_f2mulas32rs_hhll(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.83 = bitcast i64 %a to <2 x i32>
  %bc.84 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulas32rs.hhll(i64 %c, <2 x i32> %bc.83, <2 x i32> %bc.84)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32rs_hhll:
; CHECK: f2mulas32rs_hhll

define dso_local i64 @test_f2mulas32rs_hllh(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.85 = bitcast i64 %a to <2 x i32>
  %bc.86 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulas32rs.hllh(i64 %c, <2 x i32> %bc.85, <2 x i32> %bc.86)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32rs_hllh:
; CHECK: f2mulas32rs_hllh

define dso_local i64 @test_f2mulsa32rs_hhll(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.87 = bitcast i64 %a to <2 x i32>
  %bc.88 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulsa32rs.hhll(i64 %c, <2 x i32> %bc.87, <2 x i32> %bc.88)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32rs_hhll:
; CHECK: f2mulsa32rs_hhll

define dso_local i64 @test_f2mulsa32rs_hllh(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.89 = bitcast i64 %a to <2 x i32>
  %bc.90 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulsa32rs.hllh(i64 %c, <2 x i32> %bc.89, <2 x i32> %bc.90)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32rs_hllh:
; CHECK: f2mulsa32rs_hllh

;===---------------------------------------------------------------------===;
; J. F2MUL zero-accumulator variants (non-saturating + rounding)
;===---------------------------------------------------------------------===;

define dso_local i64 @test_f2mulas32r_hhll(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.91 = bitcast i64 %a to <2 x i32>
  %bc.92 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulas32r.hhll(i64 %c, <2 x i32> %bc.91, <2 x i32> %bc.92)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32r_hhll:
; CHECK: f2mulas32r_hhll

define dso_local i64 @test_f2mulas32r_hllh(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.93 = bitcast i64 %a to <2 x i32>
  %bc.94 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulas32r.hllh(i64 %c, <2 x i32> %bc.93, <2 x i32> %bc.94)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulas32r_hllh:
; CHECK: f2mulas32r_hllh

define dso_local i64 @test_f2mulsa32r_hhll(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.95 = bitcast i64 %a to <2 x i32>
  %bc.96 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulsa32r.hhll(i64 %c, <2 x i32> %bc.95, <2 x i32> %bc.96)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32r_hhll:
; CHECK: f2mulsa32r_hhll

define dso_local i64 @test_f2mulsa32r_hllh(i64 %a, i64 %b, i64 %c) {
entry:
  %bc.97 = bitcast i64 %a to <2 x i32>
  %bc.98 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulsa32r.hllh(i64 %c, <2 x i32> %bc.97, <2 x i32> %bc.98)
  ret i64 %r
}
; CHECK-LABEL: test_f2mulsa32r_hllh:
; CHECK: f2mulsa32r_hllh

;===---------------------------------------------------------------------===;
; Intrinsic declarations
;===---------------------------------------------------------------------===;

; X2 SIMD fractional multiply
declare <2 x i32> @llvm.haydn.x2fmul32rs(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmul32rss(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmul32ts(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32rss(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32ts(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32rss(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32ts(<2 x i32>, <2 x i32>, <2 x i32>)
; X4 SIMD fractional multiply
declare <4 x i16> @llvm.haydn.x4fmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fmul16ts(<4 x i16>, <4 x i16>)
; X2/X4 shift with rounding
declare <2 x i32> @llvm.haydn.x2frsst32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2frst32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4frsst16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4frst16(<4 x i16>, <4 x i16>)
; X2SRAI32R / X4SRAI16R
declare <2 x i32> @llvm.haydn.x2srai32r(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4srai16r(<4 x i16>, i32)

; FMUL16_HS remaining
declare i64 @llvm.haydn.fmul16.hs01(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs02(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs03(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs11(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs12(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs13(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs22(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs23(<4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmul16.hs33(<4 x i16>, <4 x i16>)
; FMUL16_LS
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
; FMULAA16 HS/LS MAC
declare i64 @llvm.haydn.fmulaa16.hs.13.02(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.hs.33.22(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.11.00(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.13.02(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulaa16.ls.33.22(i64, <4 x i16>, <4 x i16>)
; FMULSS16 HS/LS MSU
declare i64 @llvm.haydn.fmulss16.hs.13.02(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.hs.33.22(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.11.00(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.13.02(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.33.22(i64, <4 x i16>, <4 x i16>)
; F2MUL zero-accumulator (saturating + rounding)
declare i64 @llvm.haydn.f2mulas32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulas32rs.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulsa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulsa32rs.hllh(i64, <2 x i32>, <2 x i32>)
; F2MUL zero-accumulator (non-saturating + rounding)
declare i64 @llvm.haydn.f2mulas32r.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulas32r.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulsa32r.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulsa32r.hllh(i64, <2 x i32>, <2 x i32>)
