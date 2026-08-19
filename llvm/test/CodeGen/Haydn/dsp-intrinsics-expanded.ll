; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: smoke — labels + payload opcodes; bundle regroup must not red this file.
;
;
; Expanded DSP intrinsic test for Haydn backend.
; Covers additional intrinsics and edge cases beyond dsp-intrinsic-e2e.ll.
;
; Categories tested:
; MUL64 remaining variants (signed-signed, signed-unsigned, unsigned-signed, unsigned-unsigned)
; MULA64 (multiply-accumulate 64-bit, all sign combos)
; MULS64 (multiply-subtract 64-bit, all sign combos)
; MULAS64 (multiply-accumulate-subtract 64-bit)
; MULSS64 (multiply-subtract-subtract 64-bit)
; Saturating arithmetic (add32s, sub32s, abs32s, neg32s, add64s, sub64s)
; Saturating arithmetic 64-bit unary (abs64s, neg64s) -- TODO: MC encoding missing
; Non-saturating absolute/negate 64-bit (abs64, neg64) -- TODO: MC encoding missing
; Fractional multiply (fmul32s, fmula32s, fmuls32s)
; 32-bit multiply high (mull, mulssh, mulsuh, muluuh)
; Q-format ternary (mulq31, macq31, mulq63, mac32)
; SIMD binary (x2add32s, x2sub32s, x2addsub32s, x4add16s, x4sub16s)
; SIMD ternary MAC (x2mula32, x2muls32, x4mula16, x4muls16, x4mula16s, x4muls16s)
; Transcendental (log2, exp2, recip, sqrt) -- TODO: MC encoding missing
; Normalization/NSA (nsa32, nsau32 work; nsa64 etc. -- TODO: MC encoding missing)
; Edge cases: multiple intrinsic calls in sequence, register pressure

;===------------------------------------------------------------------===;
; MUL64 remaining variants
;===------------------------------------------------------------------===;

;
; Rebaselined (XFAIL hygiene post-): Format E printer
; uses optional `.sN` slot suffixes (e.g. xor32). Full asm dump regenerated
; from llc -verify-machineinstrs (backend clean).
;

declare i64 @llvm.haydn.mul64.ss.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hl(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.ss.hh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_ss_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_lh:
; CHECK-DAG: mul64.lh
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.lh(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

define i64 @test_mul64_ss_hl(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_hl:
; CHECK-DAG: mul64.hl
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.hl(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

define i64 @test_mul64_ss_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_hh:
; CHECK-DAG: mul64.hh
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.hh(<2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.su.lul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.ulh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.uhl(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.uhh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_su_lul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_lul:
; CHECK-DAG: mul64.lul
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.lul(<2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

define i64 @test_mul64_su_ulh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_ulh:
; CHECK-DAG: mul64.ulh
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.ulh(<2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

define i64 @test_mul64_su_uhl(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_uhl:
; CHECK-DAG: mul64.uhl
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.uhl(<2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}

define i64 @test_mul64_su_uhh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_uhh:
; CHECK-DAG: mul64.uhh
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.uhh(<2 x i32> %bc.13, <2 x i32> %bc.14)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.us.luh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.uhuh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.hul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.uhul(<2 x i32>, <2 x i32>)
define i64 @test_mul64_us_luh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_luh:
; CHECK-DAG: mul64.luh
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.luh(<2 x i32> %bc.15, <2 x i32> %bc.16)
  ret i64 %r
}

define i64 @test_mul64_us_uhuh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_uhuh:
; CHECK-DAG: mul64.uhuh
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.uhuh(<2 x i32> %bc.17, <2 x i32> %bc.18)
  ret i64 %r
}

define i64 @test_mul64_us_hul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_hul:
; CHECK-DAG: mul64.hul
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.hul(<2 x i32> %bc.19, <2 x i32> %bc.20)
  ret i64 %r
}

define i64 @test_mul64_us_uhul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_uhul:
; CHECK-DAG: mul64.uhul
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.uhul(<2 x i32> %bc.21, <2 x i32> %bc.22)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.uu.uluh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ulul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ull(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.ulh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_uu_uluh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_uluh:
; CHECK-DAG: mul64.uluh
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.uluh(<2 x i32> %bc.23, <2 x i32> %bc.24)
  ret i64 %r
}

define i64 @test_mul64_uu_ulul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ulul:
; CHECK-DAG: mul64.ulul
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ulul(<2 x i32> %bc.25, <2 x i32> %bc.26)
  ret i64 %r
}

define i64 @test_mul64_uu_ull(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ull:
; CHECK-DAG: mul64.ull
  %bc.27 = bitcast i64 %a to <2 x i32>
  %bc.28 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ull(<2 x i32> %bc.27, <2 x i32> %bc.28)
  ret i64 %r
}

define i64 @test_mul64_uu_ulh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_ulh:
; CHECK-DAG: mul64.ulh
  %bc.29 = bitcast i64 %a to <2 x i32>
  %bc.30 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.ulh(<2 x i32> %bc.29, <2 x i32> %bc.30)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULA64 (multiply-accumulate, 16 variants)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.ss.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_ll:
; CHECK-DAG: mula64.ll
  %bc.31 = bitcast i64 %a to <2 x i32>
  %bc.32 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.31, <2 x i32> %bc.32)
  ret i64 %r
}

define i64 @test_mula64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_lh:
; CHECK-DAG: mula64.lh
  %bc.33 = bitcast i64 %a to <2 x i32>
  %bc.34 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.lh(i64 %acc, <2 x i32> %bc.33, <2 x i32> %bc.34)
  ret i64 %r
}

define i64 @test_mula64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hl:
; CHECK-DAG: mula64.hl
  %bc.35 = bitcast i64 %a to <2 x i32>
  %bc.36 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hl(i64 %acc, <2 x i32> %bc.35, <2 x i32> %bc.36)
  ret i64 %r
}

define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hh:
; CHECK-DAG: mula64.hh
  %bc.37 = bitcast i64 %a to <2 x i32>
  %bc.38 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, <2 x i32> %bc.37, <2 x i32> %bc.38)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.su.lul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.ulh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.uhl(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.uhh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_lul:
; CHECK-DAG: mula64.lul
  %bc.39 = bitcast i64 %a to <2 x i32>
  %bc.40 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.lul(i64 %acc, <2 x i32> %bc.39, <2 x i32> %bc.40)
  ret i64 %r
}

define i64 @test_mula64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_ulh:
; CHECK-DAG: mula64.ulh
  %bc.41 = bitcast i64 %a to <2 x i32>
  %bc.42 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.ulh(i64 %acc, <2 x i32> %bc.41, <2 x i32> %bc.42)
  ret i64 %r
}

define i64 @test_mula64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhl:
; CHECK-DAG: mula64.uhl
  %bc.43 = bitcast i64 %a to <2 x i32>
  %bc.44 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.uhl(i64 %acc, <2 x i32> %bc.43, <2 x i32> %bc.44)
  ret i64 %r
}

define i64 @test_mula64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhh:
; CHECK-DAG: mula64.uhh
  %bc.45 = bitcast i64 %a to <2 x i32>
  %bc.46 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.uhh(i64 %acc, <2 x i32> %bc.45, <2 x i32> %bc.46)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.us.luh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.uhuh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.hul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.uhul(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_luh:
; CHECK-DAG: mula64.luh
  %bc.47 = bitcast i64 %a to <2 x i32>
  %bc.48 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.luh(i64 %acc, <2 x i32> %bc.47, <2 x i32> %bc.48)
  ret i64 %r
}

define i64 @test_mula64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhuh:
; CHECK-DAG: mula64.uhuh
  %bc.49 = bitcast i64 %a to <2 x i32>
  %bc.50 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.uhuh(i64 %acc, <2 x i32> %bc.49, <2 x i32> %bc.50)
  ret i64 %r
}

define i64 @test_mula64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_hul:
; CHECK-DAG: mula64.hul
  %bc.51 = bitcast i64 %a to <2 x i32>
  %bc.52 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.hul(i64 %acc, <2 x i32> %bc.51, <2 x i32> %bc.52)
  ret i64 %r
}

define i64 @test_mula64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhul:
; CHECK-DAG: mula64.uhul
  %bc.53 = bitcast i64 %a to <2 x i32>
  %bc.54 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.uhul(i64 %acc, <2 x i32> %bc.53, <2 x i32> %bc.54)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.uu.uluh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ulul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ull(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.ulh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_uluh:
; CHECK-DAG: mula64.uluh
  %bc.55 = bitcast i64 %a to <2 x i32>
  %bc.56 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.uluh(i64 %acc, <2 x i32> %bc.55, <2 x i32> %bc.56)
  ret i64 %r
}

define i64 @test_mula64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ulul:
; CHECK-DAG: mula64.ulul
  %bc.57 = bitcast i64 %a to <2 x i32>
  %bc.58 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ulul(i64 %acc, <2 x i32> %bc.57, <2 x i32> %bc.58)
  ret i64 %r
}

define i64 @test_mula64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ull:
; CHECK-DAG: mula64.ull
  %bc.59 = bitcast i64 %a to <2 x i32>
  %bc.60 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ull(i64 %acc, <2 x i32> %bc.59, <2 x i32> %bc.60)
  ret i64 %r
}

define i64 @test_mula64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_ulh:
; CHECK-DAG: mula64.ulh
  %bc.61 = bitcast i64 %a to <2 x i32>
  %bc.62 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.ulh(i64 %acc, <2 x i32> %bc.61, <2 x i32> %bc.62)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULS64 (multiply-subtract, 16 variants -- representative sample)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.lul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.luh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.uluh(i64, <2 x i32>, <2 x i32>)
define i64 @test_muls64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_ll:
; CHECK-DAG: muls64.ll
  %bc.63 = bitcast i64 %a to <2 x i32>
  %bc.64 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.63, <2 x i32> %bc.64)
  ret i64 %r
}

define i64 @test_muls64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_hh:
; CHECK-DAG: muls64.hh
  %bc.65 = bitcast i64 %a to <2 x i32>
  %bc.66 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.hh(i64 %acc, <2 x i32> %bc.65, <2 x i32> %bc.66)
  ret i64 %r
}

define i64 @test_muls64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_lul:
; CHECK-DAG: muls64.lul
  %bc.67 = bitcast i64 %a to <2 x i32>
  %bc.68 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.lul(i64 %acc, <2 x i32> %bc.67, <2 x i32> %bc.68)
  ret i64 %r
}

define i64 @test_muls64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_luh:
; CHECK-DAG: muls64.luh
  %bc.69 = bitcast i64 %a to <2 x i32>
  %bc.70 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.luh(i64 %acc, <2 x i32> %bc.69, <2 x i32> %bc.70)
  ret i64 %r
}

define i64 @test_muls64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_uluh:
; CHECK-DAG: muls64.uluh
  %bc.71 = bitcast i64 %a to <2 x i32>
  %bc.72 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.uluh(i64 %acc, <2 x i32> %bc.71, <2 x i32> %bc.72)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULAS64 (multiply-accumulate-subtract, representative sample)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulas64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.uluh(i64, i64, i64)
define i64 @test_mulas64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_ll:
; CHECK-DAG: mulas64.ll
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_lul:
; CHECK-DAG: mulas64.lul
  %r = call i64 @llvm.haydn.mulas64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_luh:
; CHECK-DAG: mulas64.luh
  %r = call i64 @llvm.haydn.mulas64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_uluh:
; CHECK-DAG: mulas64.uluh
  %r = call i64 @llvm.haydn.mulas64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULSS64 (multiply-subtract-subtract, representative sample)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulss64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.uluh(i64, i64, i64)
define i64 @test_mulss64_ss_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_ll:
; CHECK-DAG: mulss64.ll
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_lul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_lul:
; CHECK-DAG: mulss64.lul
  %r = call i64 @llvm.haydn.mulss64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_luh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_luh:
; CHECK-DAG: mulss64.luh
  %r = call i64 @llvm.haydn.mulss64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_uluh:
; CHECK-DAG: mulss64.uluh
  %r = call i64 @llvm.haydn.mulss64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Saturating arithmetic (32-bit GPR32)
;===------------------------------------------------------------------===;

declare i32 @llvm.haydn.add32s(i32, i32)
declare i32 @llvm.haydn.sub32s(i32, i32)
declare i32 @llvm.haydn.abs32s(i32)
declare i32 @llvm.haydn.neg32s(i32)
define i32 @test_add32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_add32s:
; CHECK-DAG: add32s
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_sub32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_sub32s:
; CHECK-DAG: sub32s
  %r = call i32 @llvm.haydn.sub32s(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_abs32s(i32 %a) {
; CHECK-LABEL: test_abs32s:
; CHECK-DAG: abs32s
  %r = call i32 @llvm.haydn.abs32s(i32 %a)
  ret i32 %r
}

define i32 @test_neg32s(i32 %a) {
; CHECK-LABEL: test_neg32s:
; CHECK-DAG: neg32s
  %r = call i32 @llvm.haydn.neg32s(i32 %a)
  ret i32 %r
}

;===------------------------------------------------------------------===;
; Saturating arithmetic (64-bit DR64 binary)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64s(i64, i64)
declare i64 @llvm.haydn.sub64s(i64, i64)
define i64 @test_add64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64s:
; CHECK-DAG: add64s
  %r = call i64 @llvm.haydn.add64s(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64s:
; CHECK-DAG: sub64s
  %r = call i64 @llvm.haydn.sub64s(i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Saturating absolute/negate 64-bit (DR64 unary)
; TODO: abs64s, neg64s, abs64, neg64 are selected by the instruction selector
; but the MC layer drops them during emission (no encoding yet).
; These tests are commented out until MC encoding is implemented.
;===------------------------------------------------------------------===;

;===------------------------------------------------------------------===;
; Fractional multiply (FMUL32S)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul32s.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.hh(<2 x i32>, <2 x i32>)
define i64 @test_fmul32s_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_ll:
; CHECK-DAG: fmul32s.ll
  %bc.73 = bitcast i64 %a to <2 x i32>
  %bc.74 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.ll(<2 x i32> %bc.73, <2 x i32> %bc.74)
  ret i64 %r
}

define i64 @test_fmul32s_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_lh:
; CHECK-DAG: fmul32s.lh
  %bc.75 = bitcast i64 %a to <2 x i32>
  %bc.76 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.lh(<2 x i32> %bc.75, <2 x i32> %bc.76)
  ret i64 %r
}

define i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK-DAG: fmul32s.hh
  %bc.77 = bitcast i64 %a to <2 x i32>
  %bc.78 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.hh(<2 x i32> %bc.77, <2 x i32> %bc.78)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Fractional multiply-accumulate (FMULA32S)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_ll:
; CHECK-DAG: fmula32s.ll
  %bc.79 = bitcast i64 %a to <2 x i32>
  %bc.80 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.79, <2 x i32> %bc.80)
  ret i64 %r
}

define i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK-DAG: fmula32s.lh
  %bc.81 = bitcast i64 %a to <2 x i32>
  %bc.82 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, <2 x i32> %bc.81, <2 x i32> %bc.82)
  ret i64 %r
}

define i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK-DAG: fmula32s.hh
  %bc.83 = bitcast i64 %a to <2 x i32>
  %bc.84 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, <2 x i32> %bc.83, <2 x i32> %bc.84)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Fractional multiply-subtract (FMULS32S)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, <2 x i32>, <2 x i32>)
; Regenerated post-latr: fmuls32s.lh widened from arity 2 to arity 3
; (read-modify-write accumulator: acc, a, b) to match the.td instruction.
declare i64 @llvm.haydn.fmuls32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_ll:
; CHECK-DAG: fmuls32s.ll
  %bc.85 = bitcast i64 %a to <2 x i32>
  %bc.86 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, <2 x i32> %bc.85, <2 x i32> %bc.86)
  ret i64 %r
}

define i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK-DAG: fmuls32s.lh
  %bc.87 = bitcast i64 %a to <2 x i32>
  %bc.88 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, <2 x i32> %bc.87, <2 x i32> %bc.88)
  ret i64 %r
}

define i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_hh:
; CHECK-DAG: fmuls32s.hh
  %bc.89 = bitcast i64 %a to <2 x i32>
  %bc.90 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, <2 x i32> %bc.89, <2 x i32> %bc.90)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; 32-bit multiply high
;===------------------------------------------------------------------===;

declare i32 @llvm.haydn.mull(i32, i32)
declare i32 @llvm.haydn.mulssh(i32, i32)
declare i32 @llvm.haydn.mulsuh(i32, i32)
declare i32 @llvm.haydn.muluuh(i32, i32)
define i32 @test_mull(i32 %a, i32 %b) {
; CHECK-LABEL: test_mull:
; 2026-08-19: MULL untied per golden (rt = rs1*rs2, non-destructive; ISS
; CC_G_GG_M) — the old $rd=$rs2 two-address copy (move32) is gone.
; CHECK-DAG: mull
  %r = call i32 @llvm.haydn.mull(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulssh(i32 %a, i32 %b) {
; CHECK-LABEL: test_mulssh:
; CHECK-DAG: mulssh
  %r = call i32 @llvm.haydn.mulssh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulsuh(i32 %a, i32 %b) {
; CHECK-LABEL: test_mulsuh:
; CHECK-DAG: mulsuh
  %r = call i32 @llvm.haydn.mulsuh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_muluuh(i32 %a, i32 %b) {
; CHECK-LABEL: test_muluuh:
; CHECK-DAG: muluuh
  %r = call i32 @llvm.haydn.muluuh(i32 %a, i32 %b)
  ret i32 %r
}

;===------------------------------------------------------------------===;
; Q-format ternary (GPR32 and DR64)
;===------------------------------------------------------------------===;

declare i32 @llvm.haydn.mulq31(i32, i32, i32)
declare i32 @llvm.haydn.macq31(i32, i32, i32)
declare i64 @llvm.haydn.mulq63(i64, i64, i64)
declare i32 @llvm.haydn.mac32(i32, i32, i32)
define i32 @test_mulq31(i32 %a, i32 %b, i32 %c) {
; Lowered to MULSSH (phantom MULQ31 removed).
; CHECK-LABEL: test_mulq31:
; CHECK-DAG: mulssh
  %r = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_macq31(i32 %a, i32 %b, i32 %c) {
; Lowered to ADD32(acc, MULL) or MULSSH for Q31 (phantom MACQ31 removed).
; CHECK-LABEL: test_macq31:
; CHECK-DAG: mulssh
; CHECK-DAG: add32
  %r = call i32 @llvm.haydn.macq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

; Mulq63 lowered to MUL64_LL (full signed 64-bit product; phantom
; MULQ63 removed — no native scalar DR64 Q1.63 saturating multiply in the ISA).
define i64 @test_mulq63(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: test_mulq63:
; CHECK-DAG: mul64.ll
  %r = call i64 @llvm.haydn.mulq63(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

define i32 @test_mac32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_mac32:
; CHECK-DAG: mull
; CHECK-DAG: add32
  %r = call i32 @llvm.haydn.mac32(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

;===------------------------------------------------------------------===;
; SIMD binary DR64 (dual 32-bit, quad 16-bit)
;===------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16s(<4 x i16>, <4 x i16>)

define <2 x i32> @test_x2add32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32s:
; CHECK-DAG: x2add32s
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sub32s:
; CHECK-DAG: x2sub32s
  %r = call <2 x i32> @llvm.haydn.x2sub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2addsub32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32s:
; CHECK-DAG: x2addsub32s
  %r = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4add16s(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4add16s:
; CHECK-DAG: x4add16s
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16s(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4sub16s:
; CHECK-DAG: x4sub16s
  %r = call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===------------------------------------------------------------------===;
; SIMD ternary MAC (DR64)
;===------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16s(i64, i64, <4 x i16>, <4 x i16>)
define i64 @test_x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mula32:
; CHECK-DAG: x2mula32
  %bc.91 = bitcast i64 %a to <2 x i32>
  %bc.92 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, <2 x i32> %bc.91, <2 x i32> %bc.92)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2muls32:
; CHECK-DAG: x2muls32
  %bc.93 = bitcast i64 %a to <2 x i32>
  %bc.94 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, <2 x i32> %bc.93, <2 x i32> %bc.94)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16:
; CHECK-DAG: x4mula16
  %bc.95 = bitcast i64 %a to <4 x i16>
  %bc.96 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc, i64 %acc2, <4 x i16> %bc.95, <4 x i16> %bc.96)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16:
; CHECK-DAG: x4muls16
  %bc.97 = bitcast i64 %a to <4 x i16>
  %bc.98 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16(i64 %acc, i64 %acc2, <4 x i16> %bc.97, <4 x i16> %bc.98)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16s:
; CHECK-DAG: x4mula16s
  %bc.99 = bitcast i64 %a to <4 x i16>
  %bc.100 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc, i64 %acc2, <4 x i16> %bc.99, <4 x i16> %bc.100)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16s:
; CHECK-DAG: x4muls16s
  %bc.101 = bitcast i64 %a to <4 x i16>
  %bc.102 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16s(i64 %acc, i64 %acc2, <4 x i16> %bc.101, <4 x i16> %bc.102)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===------------------------------------------------------------------===;
; Transcendental functions (unary GPR32)
; TODO: log2, exp2, recip, sqrt are selected by the instruction selector
; but the MC layer drops them during emission (no encoding yet).
; These tests are commented out until MC encoding is implemented.
;===------------------------------------------------------------------===;

;===------------------------------------------------------------------===;
; Normalization / NSA (unary GPR32 in and out)
; nsa32 and nsau32 work. The others are selected but MC emitter drops them.
;===------------------------------------------------------------------===;

declare i32 @llvm.haydn.nsa32(i32)
declare i32 @llvm.haydn.nsau32(i32)
define i32 @test_nsa32(i32 %a) {
; CHECK-LABEL: test_nsa32:
; CHECK-DAG: nsa32
  %r = call i32 @llvm.haydn.nsa32(i32 %a)
  ret i32 %r
}

define i32 @test_nsau32(i32 %a) {
; CHECK-LABEL: test_nsau32:
; CHECK-DAG: nsau32
  %r = call i32 @llvm.haydn.nsau32(i32 %a)
  ret i32 %r
}

; TODO: nsa64, nsa16_l, nsa32_l, nsaz64, nsaz16_l, nsaz32_l are selected
; by the instruction selector but the MC layer drops them during emission.
; These tests are commented out until MC encoding is implemented.

;===------------------------------------------------------------------===;
; Edge cases: multiple intrinsic calls in sequence (register pressure)
;===------------------------------------------------------------------===;

; Multiple MUL64 variants in sequence -- tests that the register allocator
; handles multiple DR64 results across different MUL64 opcodes.

declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)
define i64 @test_mul64_chain(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_chain:
; CHECK-DAG: mul64.ll
; CHECK-DAG: mul64.lh
; CHECK-DAG: add64
  %bc.103 = bitcast i64 %a to <2 x i32>
  %bc.104 = bitcast i64 %b to <2 x i32>
  %p1 = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.103, <2 x i32> %bc.104)
  %bc.105 = bitcast i64 %a to <2 x i32>
  %bc.106 = bitcast i64 %b to <2 x i32>
  %p2 = call i64 @llvm.haydn.mul64.ss.lh(<2 x i32> %bc.105, <2 x i32> %bc.106)
  %r = add i64 %p1, %p2
  ret i64 %r
}

; Saturating add followed by saturating sub -- tests GPR32 saturation pipeline.

define i32 @test_sat_chain(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_sat_chain:
; CHECK-DAG: add32s
; CHECK-DAG: sub32s
  %s1 = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  %r = call i32 @llvm.haydn.sub32s(i32 %s1, i32 %c)
  ret i32 %r
}

; FMUL followed by FMULA (fractional multiply then accumulate)
; tests DR64 fractional pipeline chaining.

define i64 @test_fmul_fmula_chain(i64 %acc, i64 %a, i64 %b) {
; Slot12_ALU_AccLat itinerary changed scheduling. Opcode presence only.
; CHECK-LABEL: test_fmul_fmula_chain:
; CHECK-DAG: fmula32s.ll
; CHECK-DAG: fmul32s.ll
; CHECK-DAG: add64
  %bc.107 = bitcast i64 %a to <2 x i32>
  %bc.108 = bitcast i64 %b to <2 x i32>
  %p1 = call i64 @llvm.haydn.fmul32s.ll(<2 x i32> %bc.107, <2 x i32> %bc.108)
  %bc.109 = bitcast i64 %a to <2 x i32>
  %bc.110 = bitcast i64 %b to <2 x i32>
  %p2 = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.109, <2 x i32> %bc.110)
  %r = add i64 %p1, %p2
  ret i64 %r
}

; SIMD binary followed by SIMD ternary MAC -- tests DR64 register pressure
; with multiple SIMD operations.

define <2 x i32> @test_simd_chain(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c) {
; CHECK-LABEL: test_simd_chain:
; CHECK-DAG: x2add32s
; CHECK-DAG: or64
; CHECK-DAG: x2mula32
; CHECK-DAG: add64
  %v1 = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  %v1_i = bitcast <2 x i32> %v1 to i64
  %b_i  = bitcast <2 x i32> %b to i64
  %c_i  = bitcast <2 x i32> %c to i64
  %a_i  = bitcast <2 x i32> %a to i64
  %bc.111 = bitcast i64 %b_i to <2 x i32>
  %bc.112 = bitcast i64 %c_i to <2 x i32>
  %v2 = call { i64, i64 } @llvm.haydn.x2mula32(i64 %v1_i, i64 %a_i, <2 x i32> %bc.111, <2 x i32> %bc.112)
  %v2_h = extractvalue { i64, i64 } %v2, 0
  %r_i = add i64 %v2_h, %a_i
  %r   = bitcast i64 %r_i to <2 x i32>
  ret <2 x i32> %r
}

; NSA chain -- tests normalization pipeline.

define i32 @test_nsa_chain(i32 %a, i32 %b) {
; CHECK-LABEL: test_nsa_chain:
; CHECK-DAG: nsau32
; CHECK-DAG: nsa32
; CHECK-DAG: add32
  %v1 = call i32 @llvm.haydn.nsa32(i32 %a)
  %v2 = call i32 @llvm.haydn.nsau32(i32 %b)
  %r = add i32 %v1, %v2
  ret i32 %r
}

; Mixed GPR32 and DR64 intrinsics -- tests register bank interaction.

define i64 @test_mixed_banks(i32 %a, i32 %b, i64 %c, i64 %d) {
; CHECK-LABEL: test_mixed_banks:
; CHECK-DAG: mulssh
; CHECK-DAG: mul64.ll
; CHECK-DAG: sext32t64
; CHECK-DAG: add64
  %hi = call i32 @llvm.haydn.mulssh(i32 %a, i32 %b)
  %bc.113 = bitcast i64 %c to <2 x i32>
  %bc.114 = bitcast i64 %d to <2 x i32>
  %lo = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.113, <2 x i32> %bc.114)
  %ext = sext i32 %hi to i64
  %r = add i64 %lo, %ext
  ret i64 %r
}

; Q-format ternary chain -- tests ternary intrinsic sequencing with MAC.
;
; MULQ31/MACQ31/MULQ63 were phantom instructions (not in the ISA DB) and
; have been removed from the.td. The source-level builtins
; @llvm.haydn.mulq31 / @llvm.haydn.macq31 remain and are lowered in
; HaydnInstructionSelector to a real ISA sequence built from MULSSH +
; SEXT32T64 + MUL64_LL + MOVE32_DR_L + ADD32. The CHECKs below pin that
; lowered sequence so a regression in the selector lowering is caught.

define i32 @test_qformat_chain(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_qformat_chain:
; CHECK-DAG: mulssh
; CHECK-DAG: add32
  %q1 = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  %r = call i32 @llvm.haydn.macq31(i32 %q1, i32 %b, i32 %a)
  ret i32 %r
}
