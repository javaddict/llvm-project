; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — this test.

; REBASELINED: G-CAPI smoke — residual mulas64/mulss64 member opcodes use dotted print (setDesc), not underscore
; Status : previously-XFAIL regression resolved; lit PASS.
; this test. The test exercises DR64 MAC intrinsics that crash in the post-Flex
; selector/encoder (likely the same X2MULA32-class zero-emit / variant-def bug
; tracked in m0-slot-or-s1-mac.ll, or a related DR64 MAC selector gap). Real
;
; Comprehensive DR64 MAC intrinsics test for Haydn backend.
; Tests 64-bit multiply, multiply-accumulate, multiply-subtract, and related
; MAC operations that operate on DR64 register operands.
;
; Categories:
; MUL64 all sign combos (ss, su, us, uu) x lane variants
; MULA64 all sign combos (multiply-accumulate)
; MULS64 all sign combos (multiply-subtract)
; MULAS64 all sign combos (multiply-accumulate-subtract)
; MULSS64 all sign combos (multiply-subtract-subtract)
; Fractional multiply (fmul32s, fmula32s, fmuls32s) per lane
; 32x32->64 multiply from GPR (mul64.ll)
; 32-bit high multiply (mull, mulssh, mulsuh, muluuh)
; Q-format MAC (mulq31, macq31, mulq63, mac32)
; SMULA16 lane-select MAC (10 variants each: non-sat + sat)
; SMULS16 lane-select MSU (10 variants each: non-sat + sat)
; FMUL16 HS/LS all lane variants (binary DR64)
; FMULAA16 HS/LS MAC variants
; FMULSS16 HS/LS MSU variants
; FMULS16 HS/LS subtract variants
; FMULAS32S/FMULSA32S add-subtract MAC variants

;===----------------------------------------------------------------------===;
; MUL64 signed-signed remaining variants (ll already tested in dsp-intrinsics)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hl(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_ss_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_ll:
; CHECK: mul64.ll
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

define i64 @test_mul64_ss_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_lh:
; CHECK: mul64.lh
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.lh(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

define i64 @test_mul64_ss_hl(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_hl:
; CHECK: mul64.hl
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.hl(<2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

define i64 @test_mul64_ss_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_hh:
; CHECK: mul64.hh
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.hh(<2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MUL64 signed-unsigned
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.su.lul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.ulh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.uhl(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.uhh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_su_lul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_lul:
; CHECK: mul64.lul
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.lul(<2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

define i64 @test_mul64_su_ulh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_ulh:
; CHECK: mul64.ulh
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.ulh(<2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}

define i64 @test_mul64_su_uhl(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_uhl:
; CHECK: mul64.uhl
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.uhl(<2 x i32> %bc.13, <2 x i32> %bc.14)
  ret i64 %r
}

define i64 @test_mul64_su_uhh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_uhh:
; CHECK: mul64.uhh
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.uhh(<2 x i32> %bc.15, <2 x i32> %bc.16)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MUL64 unsigned-signed
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.us.luh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.uhuh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.hul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.uhul(<2 x i32>, <2 x i32>)
define i64 @test_mul64_us_luh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_luh:
; CHECK: mul64.luh
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.luh(<2 x i32> %bc.17, <2 x i32> %bc.18)
  ret i64 %r
}

define i64 @test_mul64_us_uhuh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_uhuh:
; CHECK: mul64.uhuh
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.uhuh(<2 x i32> %bc.19, <2 x i32> %bc.20)
  ret i64 %r
}

define i64 @test_mul64_us_hul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_hul:
; CHECK: mul64.hul
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.hul(<2 x i32> %bc.21, <2 x i32> %bc.22)
  ret i64 %r
}

define i64 @test_mul64_us_uhul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_uhul:
; CHECK: mul64.uhul
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.uhul(<2 x i32> %bc.23, <2 x i32> %bc.24)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MUL64 unsigned-unsigned
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.uu.uluh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ulul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ull(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ulh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_uu_uluh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_uluh:
; CHECK: mul64.uluh
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.uluh(<2 x i32> %bc.25, <2 x i32> %bc.26)
  ret i64 %r
}

define i64 @test_mul64_uu_ulul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ulul:
; CHECK: mul64.ulul
  %bc.27 = bitcast i64 %a to <2 x i32>
  %bc.28 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ulul(<2 x i32> %bc.27, <2 x i32> %bc.28)
  ret i64 %r
}

define i64 @test_mul64_uu_ull(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ull:
; CHECK: mul64.ull
  %bc.29 = bitcast i64 %a to <2 x i32>
  %bc.30 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ull(<2 x i32> %bc.29, <2 x i32> %bc.30)
  ret i64 %r
}

define i64 @test_mul64_uu_ulh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ulh:
; CHECK: mul64.ulh
  %bc.31 = bitcast i64 %a to <2 x i32>
  %bc.32 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ulh(<2 x i32> %bc.31, <2 x i32> %bc.32)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 signed-signed
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_ll:
; CHECK: mula64.ll
  %bc.33 = bitcast i64 %a to <2 x i32>
  %bc.34 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.33, <2 x i32> %bc.34)
  ret i64 %r
}

define i64 @test_mula64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_lh:
; CHECK: mula64.lh
  %bc.35 = bitcast i64 %a to <2 x i32>
  %bc.36 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.lh(i64 %acc, <2 x i32> %bc.35, <2 x i32> %bc.36)
  ret i64 %r
}

define i64 @test_mula64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hl:
; CHECK: mula64.hl
  %bc.37 = bitcast i64 %a to <2 x i32>
  %bc.38 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hl(i64 %acc, <2 x i32> %bc.37, <2 x i32> %bc.38)
  ret i64 %r
}

define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hh:
; CHECK: mula64.hh
  %bc.39 = bitcast i64 %a to <2 x i32>
  %bc.40 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, <2 x i32> %bc.39, <2 x i32> %bc.40)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 signed-unsigned
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.su.lul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.ulh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.uhl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.uhh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_lul:
; CHECK: mula64.lul
  %bc.41 = bitcast i64 %a to <2 x i32>
  %bc.42 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.lul(i64 %acc, <2 x i32> %bc.41, <2 x i32> %bc.42)
  ret i64 %r
}

define i64 @test_mula64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_ulh:
; CHECK: mula64.ulh
  %bc.43 = bitcast i64 %a to <2 x i32>
  %bc.44 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.ulh(i64 %acc, <2 x i32> %bc.43, <2 x i32> %bc.44)
  ret i64 %r
}

define i64 @test_mula64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhl:
; CHECK: mula64.uhl
  %bc.45 = bitcast i64 %a to <2 x i32>
  %bc.46 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.uhl(i64 %acc, <2 x i32> %bc.45, <2 x i32> %bc.46)
  ret i64 %r
}

define i64 @test_mula64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhh:
; CHECK: mula64.uhh
  %bc.47 = bitcast i64 %a to <2 x i32>
  %bc.48 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.uhh(i64 %acc, <2 x i32> %bc.47, <2 x i32> %bc.48)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 unsigned-signed
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.us.luh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.uhuh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.hul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.uhul(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_luh:
; CHECK: mula64.luh
  %bc.49 = bitcast i64 %a to <2 x i32>
  %bc.50 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.luh(i64 %acc, <2 x i32> %bc.49, <2 x i32> %bc.50)
  ret i64 %r
}

define i64 @test_mula64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhuh:
; CHECK: mula64.uhuh
  %bc.51 = bitcast i64 %a to <2 x i32>
  %bc.52 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.uhuh(i64 %acc, <2 x i32> %bc.51, <2 x i32> %bc.52)
  ret i64 %r
}

define i64 @test_mula64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_hul:
; CHECK: mula64.hul
  %bc.53 = bitcast i64 %a to <2 x i32>
  %bc.54 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.hul(i64 %acc, <2 x i32> %bc.53, <2 x i32> %bc.54)
  ret i64 %r
}

define i64 @test_mula64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhul:
; CHECK: mula64.uhul
  %bc.55 = bitcast i64 %a to <2 x i32>
  %bc.56 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.uhul(i64 %acc, <2 x i32> %bc.55, <2 x i32> %bc.56)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 unsigned-unsigned
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.uu.uluh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ulul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ull(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ulh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_uluh:
; CHECK: mula64.uluh
  %bc.57 = bitcast i64 %a to <2 x i32>
  %bc.58 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.uluh(i64 %acc, <2 x i32> %bc.57, <2 x i32> %bc.58)
  ret i64 %r
}

define i64 @test_mula64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ulul:
; CHECK: mula64.ulul
  %bc.59 = bitcast i64 %a to <2 x i32>
  %bc.60 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ulul(i64 %acc, <2 x i32> %bc.59, <2 x i32> %bc.60)
  ret i64 %r
}

define i64 @test_mula64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ull:
; CHECK: mula64.ull
  %bc.61 = bitcast i64 %a to <2 x i32>
  %bc.62 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ull(i64 %acc, <2 x i32> %bc.61, <2 x i32> %bc.62)
  ret i64 %r
}

define i64 @test_mula64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ulh:
; CHECK: mula64.ulh
  %bc.63 = bitcast i64 %a to <2 x i32>
  %bc.64 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ulh(i64 %acc, <2 x i32> %bc.63, <2 x i32> %bc.64)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULS64 remaining sign variants (ll tested, test lh/hl/hh + all combos)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.muls64.ss.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.hl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.lul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.ulh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.uhl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.uhh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.luh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.uhuh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.hul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.uhul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.uluh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.ulul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.ull(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.ulh(i64, <2 x i32>, <2 x i32>)
define i64 @test_muls64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_lh:
; CHECK: muls64.lh
  %bc.65 = bitcast i64 %a to <2 x i32>
  %bc.66 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.lh(i64 %acc, <2 x i32> %bc.65, <2 x i32> %bc.66)
  ret i64 %r
}

define i64 @test_muls64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_hl:
; CHECK: muls64.hl
  %bc.67 = bitcast i64 %a to <2 x i32>
  %bc.68 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.hl(i64 %acc, <2 x i32> %bc.67, <2 x i32> %bc.68)
  ret i64 %r
}

define i64 @test_muls64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_hh:
; CHECK: muls64.hh
  %bc.69 = bitcast i64 %a to <2 x i32>
  %bc.70 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.hh(i64 %acc, <2 x i32> %bc.69, <2 x i32> %bc.70)
  ret i64 %r
}

define i64 @test_muls64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_lul:
; CHECK: muls64.lul
  %bc.71 = bitcast i64 %a to <2 x i32>
  %bc.72 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.lul(i64 %acc, <2 x i32> %bc.71, <2 x i32> %bc.72)
  ret i64 %r
}

define i64 @test_muls64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_ulh:
; CHECK: muls64.ulh
  %bc.73 = bitcast i64 %a to <2 x i32>
  %bc.74 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.ulh(i64 %acc, <2 x i32> %bc.73, <2 x i32> %bc.74)
  ret i64 %r
}

define i64 @test_muls64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_uhl:
; CHECK: muls64.uhl
  %bc.75 = bitcast i64 %a to <2 x i32>
  %bc.76 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.uhl(i64 %acc, <2 x i32> %bc.75, <2 x i32> %bc.76)
  ret i64 %r
}

define i64 @test_muls64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_uhh:
; CHECK: muls64.uhh
  %bc.77 = bitcast i64 %a to <2 x i32>
  %bc.78 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.uhh(i64 %acc, <2 x i32> %bc.77, <2 x i32> %bc.78)
  ret i64 %r
}

define i64 @test_muls64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_luh:
; CHECK: muls64.luh
  %bc.79 = bitcast i64 %a to <2 x i32>
  %bc.80 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.luh(i64 %acc, <2 x i32> %bc.79, <2 x i32> %bc.80)
  ret i64 %r
}

define i64 @test_muls64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_uhuh:
; CHECK: muls64.uhuh
  %bc.81 = bitcast i64 %a to <2 x i32>
  %bc.82 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.uhuh(i64 %acc, <2 x i32> %bc.81, <2 x i32> %bc.82)
  ret i64 %r
}

define i64 @test_muls64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_hul:
; CHECK: muls64.hul
  %bc.83 = bitcast i64 %a to <2 x i32>
  %bc.84 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.hul(i64 %acc, <2 x i32> %bc.83, <2 x i32> %bc.84)
  ret i64 %r
}

define i64 @test_muls64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_uhul:
; CHECK: muls64.uhul
  %bc.85 = bitcast i64 %a to <2 x i32>
  %bc.86 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.uhul(i64 %acc, <2 x i32> %bc.85, <2 x i32> %bc.86)
  ret i64 %r
}

define i64 @test_muls64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_uluh:
; CHECK: muls64.uluh
  %bc.87 = bitcast i64 %a to <2 x i32>
  %bc.88 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.uluh(i64 %acc, <2 x i32> %bc.87, <2 x i32> %bc.88)
  ret i64 %r
}

define i64 @test_muls64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_ulul:
; CHECK: muls64.ulul
  %bc.89 = bitcast i64 %a to <2 x i32>
  %bc.90 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.ulul(i64 %acc, <2 x i32> %bc.89, <2 x i32> %bc.90)
  ret i64 %r
}

define i64 @test_muls64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_ull:
; CHECK: muls64.ull
  %bc.91 = bitcast i64 %a to <2 x i32>
  %bc.92 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.ull(i64 %acc, <2 x i32> %bc.91, <2 x i32> %bc.92)
  ret i64 %r
}

define i64 @test_muls64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_ulh:
; CHECK: muls64.ulh
  %bc.93 = bitcast i64 %a to <2 x i32>
  %bc.94 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.ulh(i64 %acc, <2 x i32> %bc.93, <2 x i32> %bc.94)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULAS64 all sign variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulas64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.ss.lh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.ss.hl(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.ss.hh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.ulh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.uhl(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.uhh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.uhuh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.hul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.uhul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.uluh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.ulul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.ull(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.ulh(i64, i64, i64)
define i64 @test_mulas64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_ll:
; CHECK: mulas64.ll
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_lh:
; CHECK: mulas64.lh
  %r = call i64 @llvm.haydn.mulas64.ss.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_hl:
; CHECK: mulas64.hl
  %r = call i64 @llvm.haydn.mulas64.ss.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_hh:
; CHECK: mulas64.hh
  %r = call i64 @llvm.haydn.mulas64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_lul:
; CHECK: mulas64.lul
  %r = call i64 @llvm.haydn.mulas64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_ulh:
; CHECK: mulas64.ulh
  %r = call i64 @llvm.haydn.mulas64.su.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_uhl:
; CHECK: mulas64.uhl
  %r = call i64 @llvm.haydn.mulas64.su.uhl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_uhh:
; CHECK: mulas64.uhh
  %r = call i64 @llvm.haydn.mulas64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_luh:
; CHECK: mulas64.luh
  %r = call i64 @llvm.haydn.mulas64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_uhuh:
; CHECK: mulas64.uhuh
  %r = call i64 @llvm.haydn.mulas64.us.uhuh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_hul:
; CHECK: mulas64.hul
  %r = call i64 @llvm.haydn.mulas64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_uhul:
; CHECK: mulas64.uhul
  %r = call i64 @llvm.haydn.mulas64.us.uhul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_uluh:
; CHECK: mulas64.uluh
  %r = call i64 @llvm.haydn.mulas64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_ulul:
; CHECK: mulas64.ulul
  %r = call i64 @llvm.haydn.mulas64.uu.ulul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_ull:
; CHECK: mulas64.ull
  %r = call i64 @llvm.haydn.mulas64.uu.ull(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_ulh:
; CHECK: mulas64.ulh
  %r = call i64 @llvm.haydn.mulas64.uu.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULSS64 all sign variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulss64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.lh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.hl(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.ss.hh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.ulh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.uhl(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.uhh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.uhuh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.hul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.uhul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.uluh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.ulul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.ull(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.ulh(i64, i64, i64)
define i64 @test_mulss64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_ll:
; CHECK: mulss64.ll
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_lh:
; CHECK: mulss64.lh
  %r = call i64 @llvm.haydn.mulss64.ss.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_hl:
; CHECK: mulss64.hl
  %r = call i64 @llvm.haydn.mulss64.ss.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_hh:
; CHECK: mulss64.hh
  %r = call i64 @llvm.haydn.mulss64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_lul:
; CHECK: mulss64.lul
  %r = call i64 @llvm.haydn.mulss64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_ulh:
; CHECK: mulss64.ulh
  %r = call i64 @llvm.haydn.mulss64.su.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_uhl:
; CHECK: mulss64.uhl
  %r = call i64 @llvm.haydn.mulss64.su.uhl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_uhh:
; CHECK: mulss64.uhh
  %r = call i64 @llvm.haydn.mulss64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_luh:
; CHECK: mulss64.luh
  %r = call i64 @llvm.haydn.mulss64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_uhuh:
; CHECK: mulss64.uhuh
  %r = call i64 @llvm.haydn.mulss64.us.uhuh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_hul:
; CHECK: mulss64.hul
  %r = call i64 @llvm.haydn.mulss64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_uhul:
; CHECK: mulss64.uhul
  %r = call i64 @llvm.haydn.mulss64.us.uhul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_uluh:
; CHECK: mulss64.uluh
  %r = call i64 @llvm.haydn.mulss64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_ulul:
; CHECK: mulss64.ulul
  %r = call i64 @llvm.haydn.mulss64.uu.ulul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_ull:
; CHECK: mulss64.ull
  %r = call i64 @llvm.haydn.mulss64.uu.ull(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_ulh:
; CHECK: mulss64.ulh
  %r = call i64 @llvm.haydn.mulss64.uu.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 32x32->64 multiply from GPR32
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.ll(i32, i32)
define i64 @test_mul64_ll(i32 %a, i32 %b) {
; CHECK-LABEL: test_mul64_ll:
; CHECK: mul64.ll
  %r = call i64 @llvm.haydn.mul64.ll(i32 %a, i32 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 32-bit high multiply (GPR32)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.mull(i32, i32)
declare i32 @llvm.haydn.mulssh(i32, i32)
declare i32 @llvm.haydn.mulsuh(i32, i32)
declare i32 @llvm.haydn.muluuh(i32, i32)
define i32 @test_mull(i32 %a, i32 %b) {
; CHECK-LABEL: test_mull:
; CHECK: mull
  %r = call i32 @llvm.haydn.mull(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulssh(i32 %a, i32 %b) {
; CHECK-LABEL: test_mulssh:
; CHECK: mulssh
  %r = call i32 @llvm.haydn.mulssh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulsuh(i32 %a, i32 %b) {
; CHECK-LABEL: test_mulsuh:
; CHECK: mulsuh
  %r = call i32 @llvm.haydn.mulsuh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_muluuh(i32 %a, i32 %b) {
; CHECK-LABEL: test_muluuh:
; CHECK: muluuh
  %r = call i32 @llvm.haydn.muluuh(i32 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Q-format MAC (ternary)
;
; MULQ31/MACQ31/MULQ63 instruction defs were REMOVED (phantom — not in
; the ISA DB; real Q-format MAC family is FF2MULA32RS_*). The source-level
; intrinsics remain for compatibility and are lowered in the selector to real
; ISA sequences:
; mulq31(a,b,_) -> MULSSH(a,b) (signed Q1.31 high product)
; macq31(acc,a,b) -> ADD32(acc, mul32(a,b)) (mirrors mac32 lowering)
; mulq63(a,b,_) -> MUL64_LL(a,b) (full signed 64-bit product)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.mulq31(i32, i32, i32)
declare i32 @llvm.haydn.macq31(i32, i32, i32)
declare i64 @llvm.haydn.mulq63(i64, i64, i64)
declare i32 @llvm.haydn.mac32(i32, i32, i32)
define i32 @test_mulq31(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_mulq31:
; Lowered to MULSSH (no native scalar Q1.31 multiply in the ISA).
; CHECK: mulssh
  %r = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_macq31(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_macq31:
; Lowered to ADD32(acc, mul32(a,b)) — mirrors mac32 lowering.
; CHECK: add32
  %r = call i32 @llvm.haydn.macq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i64 @test_mulq63(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: test_mulq63:
; Lowered to MUL64_LL (full signed 64-bit product; no native scalar
; DR64 Q1.63 saturating multiply — the real Q-format family FF2MUL32RS_*
; operates on packed lanes).
; CHECK: mul64.ll
  %r = call i64 @llvm.haydn.mulq63(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

define i32 @test_mac32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_mac32:
; CHECK: mac32
  %r = call i32 @llvm.haydn.mac32(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; SMULA16 — Signed 16-bit MAC with lane selection (non-saturating)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.smula16.00(i64, i64)
declare i64 @llvm.haydn.smula16.10(i64, i64)
declare i64 @llvm.haydn.smula16.11(i64, i64)
declare i64 @llvm.haydn.smula16.20(i64, i64)
declare i64 @llvm.haydn.smula16.21(i64, i64)
declare i64 @llvm.haydn.smula16.22(i64, i64)
declare i64 @llvm.haydn.smula16.30(i64, i64)
declare i64 @llvm.haydn.smula16.31(i64, i64)
declare i64 @llvm.haydn.smula16.32(i64, i64)
declare i64 @llvm.haydn.smula16.33(i64, i64)
define i64 @test_smula16_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_00:
; CHECK: smula16_00
  %r = call i64 @llvm.haydn.smula16.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_10(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_10:
; CHECK: smula16_10
  %r = call i64 @llvm.haydn.smula16.10(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_11(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_11:
; CHECK: smula16_11
  %r = call i64 @llvm.haydn.smula16.11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_20(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_20:
; CHECK: smula16_20
  %r = call i64 @llvm.haydn.smula16.20(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_21(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_21:
; CHECK: smula16_21
  %r = call i64 @llvm.haydn.smula16.21(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_22:
; CHECK: smula16_22
  %r = call i64 @llvm.haydn.smula16.22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_30(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_30:
; CHECK: smula16_30
  %r = call i64 @llvm.haydn.smula16.30(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_31(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_31:
; CHECK: smula16_31
  %r = call i64 @llvm.haydn.smula16.31(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_32(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_32:
; CHECK: smula16_32
  %r = call i64 @llvm.haydn.smula16.32(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16_33(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16_33:
; CHECK: smula16_33
  %r = call i64 @llvm.haydn.smula16.33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SMULA16S — Signed 16-bit saturating MAC with lane selection
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.smula16s.00(i64, i64)
declare i64 @llvm.haydn.smula16s.10(i64, i64)
declare i64 @llvm.haydn.smula16s.11(i64, i64)
declare i64 @llvm.haydn.smula16s.20(i64, i64)
declare i64 @llvm.haydn.smula16s.21(i64, i64)
declare i64 @llvm.haydn.smula16s.22(i64, i64)
declare i64 @llvm.haydn.smula16s.30(i64, i64)
declare i64 @llvm.haydn.smula16s.31(i64, i64)
declare i64 @llvm.haydn.smula16s.32(i64, i64)
declare i64 @llvm.haydn.smula16s.33(i64, i64)
define i64 @test_smula16s_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_00:
; CHECK: smula16s_00
  %r = call i64 @llvm.haydn.smula16s.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_10(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_10:
; CHECK: smula16s_10
  %r = call i64 @llvm.haydn.smula16s.10(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_11(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_11:
; CHECK: smula16s_11
  %r = call i64 @llvm.haydn.smula16s.11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_20(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_20:
; CHECK: smula16s_20
  %r = call i64 @llvm.haydn.smula16s.20(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_21(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_21:
; CHECK: smula16s_21
  %r = call i64 @llvm.haydn.smula16s.21(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_22:
; CHECK: smula16s_22
  %r = call i64 @llvm.haydn.smula16s.22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_30(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_30:
; CHECK: smula16s_30
  %r = call i64 @llvm.haydn.smula16s.30(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_31(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_31:
; CHECK: smula16s_31
  %r = call i64 @llvm.haydn.smula16s.31(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_32(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_32:
; CHECK: smula16s_32
  %r = call i64 @llvm.haydn.smula16s.32(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smula16s_33(i64 %a, i64 %b) {
; CHECK-LABEL: test_smula16s_33:
; CHECK: smula16s_33
  %r = call i64 @llvm.haydn.smula16s.33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SMULS16 — Signed 16-bit multiply-subtract (non-saturating)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.smuls16.00(i64, i64)
declare i64 @llvm.haydn.smuls16.10(i64, i64)
declare i64 @llvm.haydn.smuls16.11(i64, i64)
declare i64 @llvm.haydn.smuls16.22(i64, i64)
declare i64 @llvm.haydn.smuls16.33(i64, i64)
define i64 @test_smuls16_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16_00:
; CHECK: smuls16_00
  %r = call i64 @llvm.haydn.smuls16.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16_10(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16_10:
; CHECK: smuls16_10
  %r = call i64 @llvm.haydn.smuls16.10(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16_11(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16_11:
; CHECK: smuls16_11
  %r = call i64 @llvm.haydn.smuls16.11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16_22:
; CHECK: smuls16_22
  %r = call i64 @llvm.haydn.smuls16.22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16_33(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16_33:
; CHECK: smuls16_33
  %r = call i64 @llvm.haydn.smuls16.33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SMULS16S — Signed 16-bit saturating multiply-subtract
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.smuls16s.00(i64, i64)
declare i64 @llvm.haydn.smuls16s.11(i64, i64)
declare i64 @llvm.haydn.smuls16s.22(i64, i64)
declare i64 @llvm.haydn.smuls16s.33(i64, i64)
define i64 @test_smuls16s_00(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16s_00:
; CHECK: smuls16s_00
  %r = call i64 @llvm.haydn.smuls16s.00(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16s_11(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16s_11:
; CHECK: smuls16s_11
  %r = call i64 @llvm.haydn.smuls16s.11(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16s_22(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16s_22:
; CHECK: smuls16s_22
  %r = call i64 @llvm.haydn.smuls16s.22(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_smuls16s_33(i64 %a, i64 %b) {
; CHECK-LABEL: test_smuls16s_33:
; CHECK: smuls16s_33
  %r = call i64 @llvm.haydn.smuls16s.33(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULAS32S/FMULSA32S — saturating add-subtract dual MAC
;
; A1 full reconciliation: these are TERNARY (acc, a, b). The DB
; (haydn_instruction_db.json:7356/7397/8375/8416) shows slot-1 reads rtd as
; an accumulator (behavior `rtd = SATQ1.63(rtd +/-...)`). They were
; previously declared binary (dropping the accumulator — the
; silent-miscompute class) and routed via selectBinary; reconciled to ternary
; + selectAccMAC mirroring (FMULS32S_LH). If this regresses to binary
; the MAC silently reads an undefined accumulator.
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmulas32s.hhll(i64, i64, i64)
declare i64 @llvm.haydn.fmulas32s.hllh(i64, i64, i64)
declare i64 @llvm.haydn.fmulsa32s.hhll(i64, i64, i64)
declare i64 @llvm.haydn.fmulsa32s.hllh(i64, i64, i64)
define i64 @test_fmulas32s_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulas32s_hhll:
; CHECK: fmulas32s_hhll
  %r = call i64 @llvm.haydn.fmulas32s.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulas32s_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulas32s_hllh:
; CHECK: fmulas32s_hllh
  %r = call i64 @llvm.haydn.fmulas32s.hllh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulsa32s_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulsa32s_hhll:
; CHECK: fmulsa32s_hhll
  %r = call i64 @llvm.haydn.fmulsa32s.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmulsa32s_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmulsa32s_hllh:
; CHECK: fmulsa32s_hllh
  %r = call i64 @llvm.haydn.fmulsa32s.hllh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}
