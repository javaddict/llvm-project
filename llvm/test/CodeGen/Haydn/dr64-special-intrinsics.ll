; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Comprehensive DR64 special operations intrinsics test for Haydn backend.

; Comprehensive DR64 special operations intrinsics test for Haydn backend.
; Tests NSA variants, transcendental functions, pack/sat operations
; complex multiply, X4 CMUL F2 variants, and 32-bit saturating operations.
;
; Categories:
; NSA normalization: nsa32/nsau32 (i32); nsa64/nsa*_l/nsaz* (i64 source)
; Transcendental: log2, exp2, recip, sqrt
; X4 pack/sat: x4sat32t16
; X4 CMUL F2 (unary DR64): x4cmul16_f2, x4cmul16s_f2
; X4 SEL: x4sel16 (variable mask) / x4seli16 (const uimm4 ImmArg)
; 32-bit saturating: add32s, sub32s, abs32s, neg32s
; FMUL32S fractional multiply: ll, lh, hh
; FMULA32S fractional MAC: ll, lh, hh
; FMULS32S fractional MSU: ll, lh, hh
; FF2 fractional multiply (saturating + non-saturating, all lane variants)
; F2MUL fused dual MAC (IIR biquad)
; F2MULAA/F2MULSS fused dual MAC
; SRAI64R shift with rounding

;===----------------------------------------------------------------------===;
; NSA normalization
; nsa32/nsau32: i32 -> i32; nsa64 and *_l/nsaz*: i64 source -> i32
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.nsa32(i32)
declare i32 @llvm.haydn.nsau32(i32)
declare i32 @llvm.haydn.nsa64(i64)
declare i32 @llvm.haydn.nsa16.l(i64)
declare i32 @llvm.haydn.nsa32.l(i64)
declare i32 @llvm.haydn.nsaz64(i64)
declare i32 @llvm.haydn.nsaz16.l(i64)
declare i32 @llvm.haydn.nsaz32.l(i64)
define dso_local i32 @test_nsa32(i32 %a) {
; CHECK-LABEL: test_nsa32:
; CHECK: nsa32
  %r = call i32 @llvm.haydn.nsa32(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsau32(i32 %a) {
; CHECK-LABEL: test_nsau32:
; CHECK: nsau32
  %r = call i32 @llvm.haydn.nsau32(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsa64(i64 %a) {
; CHECK-LABEL: test_nsa64:
; CHECK: nsa64
  %r = call i32 @llvm.haydn.nsa64(i64 %a)
  ret i32 %r
}

define dso_local i32 @test_nsa16_l(i64 %a) {
; CHECK-LABEL: test_nsa16_l:
; CHECK: nsa16_l
  %r = call i32 @llvm.haydn.nsa16.l(i64 %a)
  ret i32 %r
}

define dso_local i32 @test_nsa32_l(i64 %a) {
; CHECK-LABEL: test_nsa32_l:
; CHECK: nsa32_l
  %r = call i32 @llvm.haydn.nsa32.l(i64 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz64(i64 %a) {
; CHECK-LABEL: test_nsaz64:
; CHECK: nsaz64
  %r = call i32 @llvm.haydn.nsaz64(i64 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz16_l(i64 %a) {
; CHECK-LABEL: test_nsaz16_l:
; CHECK: nsaz16_l
  %r = call i32 @llvm.haydn.nsaz16.l(i64 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz32_l(i64 %a) {
; CHECK-LABEL: test_nsaz32_l:
; CHECK: nsaz32_l
  %r = call i32 @llvm.haydn.nsaz32.l(i64 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Transcendental functions (GPR32 unary: i32 -> i32)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.log2(i32)
declare i32 @llvm.haydn.exp2(i32)
declare i32 @llvm.haydn.recip(i32)
declare i32 @llvm.haydn.sqrt(i32)
define dso_local i32 @test_log2(i32 %a) {
; CHECK-LABEL: test_log2:
; CHECK: log2
  %r = call i32 @llvm.haydn.log2(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_exp2(i32 %a) {
; CHECK-LABEL: test_exp2:
; CHECK: exp2
  %r = call i32 @llvm.haydn.exp2(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_recip(i32 %a) {
; CHECK-LABEL: test_recip:
; CHECK: recip
  %r = call i32 @llvm.haydn.recip(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_sqrt(i32 %a) {
; CHECK-LABEL: test_sqrt:
; CHECK: sqrt
  %r = call i32 @llvm.haydn.sqrt(i32 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; X4 pack/sat (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32>, <2 x i32>)
define dso_local i64 @test_x4sat32t16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4sat32t16:
; CHECK: x4sat32t16
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %r = bitcast <4 x i16> %call.3 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X4 CMUL F2 variants (unary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x4cmul16.f2(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s.f2(<4 x i16>)
define dso_local i64 @test_x4cmul16_f2(i64 %a) {
; CHECK-LABEL: test_x4cmul16_f2:
; CHECK: x4cmul16.f2
  %bc.4 = bitcast i64 %a to <4 x i16>
  %call.5 = call <2 x i32> @llvm.haydn.x4cmul16.f2(<4 x i16> %bc.4)
  %r = bitcast <2 x i32> %call.5 to i64
  ret i64 %r
}

define dso_local i64 @test_x4cmul16s_f2(i64 %a) {
; CHECK-LABEL: test_x4cmul16s_f2:
; CHECK: x4cmul16s_f2
  %bc.6 = bitcast i64 %a to <4 x i16>
  %call.7 = call <2 x i32> @llvm.haydn.x4cmul16s.f2(<4 x i16> %bc.6)
  %r = bitcast <2 x i32> %call.7 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 CMUL F2 variants (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32>, <2 x i32>)
define dso_local i64 @test_x2cmul32_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32_f2:
; CHECK: x2cmul32_f2
  %bc.8 = bitcast i64 %a to <2 x i32>
  %bc.9 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32> %bc.8, <2 x i32> %bc.9)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x2cmul32s_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32s_f2:
; CHECK: x2cmul32s_f2
  %bc.10 = bitcast i64 %a to <2 x i32>
  %bc.11 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32> %bc.10, <2 x i32> %bc.11)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; X4 SEL / SELI16 — variable mask → x4sel16; const uimm4 ImmArg → x4seli16
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4sel16(<4 x i16>, <4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

define dso_local <4 x i16> @test_x4sel16(<4 x i16> %a, <4 x i16> %b, i32 %sel) {
; CHECK-LABEL: test_x4sel16:
; Variable mask uses the reg twin; mask is a GPR (not an ImmArg).
; CHECK: x4sel16{{.*}}, {{r[0-9]+}}
; CHECK: .size test_x4sel16
  %r = call <4 x i16> @llvm.haydn.x4sel16(<4 x i16> %a, <4 x i16> %b, i32 %sel)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_imm(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16_imm:
; Const uimm4 ImmArg folds to a bare immediate (variable would fail verify).
; CHECK: x4seli16{{.*}}, 5
; CHECK: .size test_x4seli16_imm
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a, <4 x i16> %b, i32 5)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; 32-bit saturating add/sub (GPR32 binary)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.add32s(i32, i32)
declare i32 @llvm.haydn.sub32s(i32, i32)
define dso_local i32 @test_add32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_add32s:
; CHECK: add32s
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}

define dso_local i32 @test_sub32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_sub32s:
; CHECK: sub32s
  %r = call i32 @llvm.haydn.sub32s(i32 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; 32-bit saturating abs/neg (GPR32 unary)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.abs32s(i32)
declare i32 @llvm.haydn.neg32s(i32)
define dso_local i32 @test_abs32s(i32 %a) {
; CHECK-LABEL: test_abs32s:
; CHECK: abs32s
  %r = call i32 @llvm.haydn.abs32s(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_neg32s(i32 %a) {
; CHECK-LABEL: test_neg32s:
; CHECK: neg32s
  %r = call i32 @llvm.haydn.neg32s(i32 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; FMUL32S — Fractional 32x32->64 multiply (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul32s.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.hh(<2 x i32>, <2 x i32>)
define dso_local i64 @test_fmul32s_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_ll:
; CHECK: fmul32s_ll
  %bc.12 = bitcast i64 %a to <2 x i32>
  %bc.13 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.ll(<2 x i32> %bc.12, <2 x i32> %bc.13)
  ret i64 %r
}

define dso_local i64 @test_fmul32s_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_lh:
; CHECK: fmul32s_lh
  %bc.14 = bitcast i64 %a to <2 x i32>
  %bc.15 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.lh(<2 x i32> %bc.14, <2 x i32> %bc.15)
  ret i64 %r
}

define dso_local i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK: fmul32s_hh
  %bc.16 = bitcast i64 %a to <2 x i32>
  %bc.17 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.hh(<2 x i32> %bc.16, <2 x i32> %bc.17)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULA32S — Fractional 32x32->64 MAC (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.hh(i64, <2 x i32>, <2 x i32>)
define dso_local i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_ll:
; CHECK: fmula32s_ll
  %bc.18 = bitcast i64 %a to <2 x i32>
  %bc.19 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.18, <2 x i32> %bc.19)
  ret i64 %r
}

define dso_local i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK: fmula32s_lh
  %bc.20 = bitcast i64 %a to <2 x i32>
  %bc.21 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, <2 x i32> %bc.20, <2 x i32> %bc.21)
  ret i64 %r
}

define dso_local i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK: fmula32s_hh
  %bc.22 = bitcast i64 %a to <2 x i32>
  %bc.23 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, <2 x i32> %bc.22, <2 x i32> %bc.23)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULS32S — Fractional 32x32->64 MSU (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.hh(i64, <2 x i32>, <2 x i32>)
define dso_local i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_ll:
; CHECK: fmuls32s_ll
  %bc.24 = bitcast i64 %a to <2 x i32>
  %bc.25 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, <2 x i32> %bc.24, <2 x i32> %bc.25)
  ret i64 %r
}

define dso_local i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK: fmuls32s_lh
  %bc.26 = bitcast i64 %a to <2 x i32>
  %bc.27 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, <2 x i32> %bc.26, <2 x i32> %bc.27)
  ret i64 %r
}

define dso_local i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_hh:
; CHECK: fmuls32s_hh
  %bc.28 = bitcast i64 %a to <2 x i32>
  %bc.29 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, <2 x i32> %bc.28, <2 x i32> %bc.29)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FF2 fractional multiply (saturating + rounding, all lane variants)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32rs.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mul32rs.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mul32rs.hh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32rs.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32rs.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32rs.hh(i64, <2 x i32>, <2 x i32>)
define dso_local i64 @test_ff2mul32rs_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_ll:
; CHECK: ff2mul32rs_ll
  %bc.30 = bitcast i64 %a to <2 x i32>
  %bc.31 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32rs.ll(<2 x i32> %bc.30, <2 x i32> %bc.31)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32rs_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_lh:
; CHECK: ff2mul32rs_lh
  %bc.32 = bitcast i64 %a to <2 x i32>
  %bc.33 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32rs.lh(<2 x i32> %bc.32, <2 x i32> %bc.33)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32rs_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_hh:
; CHECK: ff2mul32rs_hh
  %bc.34 = bitcast i64 %a to <2 x i32>
  %bc.35 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32rs.hh(<2 x i32> %bc.34, <2 x i32> %bc.35)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_ll:
; CHECK: ff2mula32rs_ll
  %bc.36 = bitcast i64 %a to <2 x i32>
  %bc.37 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.ll(i64 %acc, <2 x i32> %bc.36, <2 x i32> %bc.37)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_lh:
; CHECK: ff2mula32rs_lh
  %bc.38 = bitcast i64 %a to <2 x i32>
  %bc.39 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, <2 x i32> %bc.38, <2 x i32> %bc.39)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_hh:
; CHECK: ff2mula32rs_hh
  %bc.40 = bitcast i64 %a to <2 x i32>
  %bc.41 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.hh(i64 %acc, <2 x i32> %bc.40, <2 x i32> %bc.41)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_ll:
; CHECK: ff2muls32rs_ll
  %bc.42 = bitcast i64 %a to <2 x i32>
  %bc.43 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32rs.ll(i64 %acc, <2 x i32> %bc.42, <2 x i32> %bc.43)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_lh:
; CHECK: ff2muls32rs_lh
  %bc.44 = bitcast i64 %a to <2 x i32>
  %bc.45 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32rs.lh(i64 %acc, <2 x i32> %bc.44, <2 x i32> %bc.45)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_hh:
; CHECK: ff2muls32rs_hh
  %bc.46 = bitcast i64 %a to <2 x i32>
  %bc.47 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32rs.hh(i64 %acc, <2 x i32> %bc.46, <2 x i32> %bc.47)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FF2 fractional multiply (non-saturating + rounding, all lane variants)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32r.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mul32r.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mul32r.hh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32r.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32r.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32r.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2muls32r.hh(i64, <2 x i32>, <2 x i32>)
define dso_local i64 @test_ff2mul32r_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_ll:
; CHECK: ff2mul32r_ll
  %bc.48 = bitcast i64 %a to <2 x i32>
  %bc.49 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32r.ll(<2 x i32> %bc.48, <2 x i32> %bc.49)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32r_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_lh:
; CHECK: ff2mul32r_lh
  %bc.50 = bitcast i64 %a to <2 x i32>
  %bc.51 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32r.lh(<2 x i32> %bc.50, <2 x i32> %bc.51)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32r_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_hh:
; CHECK: ff2mul32r_hh
  %bc.52 = bitcast i64 %a to <2 x i32>
  %bc.53 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mul32r.hh(<2 x i32> %bc.52, <2 x i32> %bc.53)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_ll:
; CHECK: ff2mula32r_ll
  %bc.54 = bitcast i64 %a to <2 x i32>
  %bc.55 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32r.ll(i64 %acc, <2 x i32> %bc.54, <2 x i32> %bc.55)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_lh:
; CHECK: ff2mula32r_lh
  %bc.56 = bitcast i64 %a to <2 x i32>
  %bc.57 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32r.lh(i64 %acc, <2 x i32> %bc.56, <2 x i32> %bc.57)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_hh:
; CHECK: ff2mula32r_hh
  %bc.58 = bitcast i64 %a to <2 x i32>
  %bc.59 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32r.hh(i64 %acc, <2 x i32> %bc.58, <2 x i32> %bc.59)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_ll:
; CHECK: ff2muls32r_ll
  %bc.60 = bitcast i64 %a to <2 x i32>
  %bc.61 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32r.ll(i64 %acc, <2 x i32> %bc.60, <2 x i32> %bc.61)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_lh:
; CHECK: ff2muls32r_lh
  %bc.62 = bitcast i64 %a to <2 x i32>
  %bc.63 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32r.lh(i64 %acc, <2 x i32> %bc.62, <2 x i32> %bc.63)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_hh:
; CHECK: ff2muls32r_hh
  %bc.64 = bitcast i64 %a to <2 x i32>
  %bc.65 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2muls32r.hh(i64 %acc, <2 x i32> %bc.64, <2 x i32> %bc.65)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; F2MULAA/F2MULSS fused dual MAC (IIR biquad, binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulaa32rs.hllh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulss32rs.hhll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.f2mulss32rs.hllh(i64, <2 x i32>, <2 x i32>)
define dso_local i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK: f2mulaa32rs_hhll
  %bc.66 = bitcast i64 %a to <2 x i32>
  %bc.67 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.66, <2 x i32> %bc.67)
  ret i64 %r
}

define dso_local i64 @test_f2mulaa32rs_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hllh:
; CHECK: f2mulaa32rs_hllh
  %bc.68 = bitcast i64 %a to <2 x i32>
  %bc.69 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulaa32rs.hllh(i64 %acc, <2 x i32> %bc.68, <2 x i32> %bc.69)
  ret i64 %r
}

define dso_local i64 @test_f2mulss32rs_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulss32rs_hhll:
; CHECK: f2mulss32rs_hhll
  %bc.70 = bitcast i64 %a to <2 x i32>
  %bc.71 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulss32rs.hhll(i64 %acc, <2 x i32> %bc.70, <2 x i32> %bc.71)
  ret i64 %r
}

define dso_local i64 @test_f2mulss32rs_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulss32rs_hllh:
; CHECK: f2mulss32rs_hllh
  %bc.72 = bitcast i64 %a to <2 x i32>
  %bc.73 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.f2mulss32rs.hllh(i64 %acc, <2 x i32> %bc.72, <2 x i32> %bc.73)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SRAI64R — 64-bit shift-right with rounding (i64, i32 -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.srai64r(i64, i32)
define dso_local i64 @test_srai64r(i64 %a) {
; CHECK-LABEL: test_srai64r:
; CHECK: srai64r
  %r = call i64 @llvm.haydn.srai64r(i64 %a, i32 8)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X4FCMUL16RS/X4FCMULA16RS — Quad 16-bit complex multiply (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16>, <4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16>, <4 x i16>, <4 x i16>)
define dso_local i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rs:
; CHECK: x4fcmul16rs
  %bc.74 = bitcast i64 %a to <4 x i16>
  %bc.75 = bitcast i64 %b to <4 x i16>
  %call.76 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.74, <4 x i16> %bc.75)
  %r = bitcast <4 x i16> %call.76 to i64
  ret i64 %r
}

define dso_local i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rs:
; CHECK: x4fcmula16rs
; Ternary accumulator form (acc, a, b) — reads rtd per DB.
  %bc.77 = bitcast i64 %acc to <4 x i16>
  %bc.78 = bitcast i64 %a to <4 x i16>
  %bc.79 = bitcast i64 %b to <4 x i16>
  %call.80 = call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %bc.77, <4 x i16> %bc.78, <4 x i16> %bc.79)
  %r = bitcast <4 x i16> %call.80 to i64
  ret i64 %r
}

define dso_local i64 @test_x4fcmul16rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rss:
; CHECK: x4fcmul16rss
  %bc.81 = bitcast i64 %a to <4 x i16>
  %bc.82 = bitcast i64 %b to <4 x i16>
  %call.83 = call <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16> %bc.81, <4 x i16> %bc.82)
  %r = bitcast <4 x i16> %call.83 to i64
  ret i64 %r
}

define dso_local i64 @test_x4fcmula16rss(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rss:
; CHECK: x4fcmula16rss
; Ternary accumulator form.
  %bc.84 = bitcast i64 %acc to <4 x i16>
  %bc.85 = bitcast i64 %a to <4 x i16>
  %bc.86 = bitcast i64 %b to <4 x i16>
  %call.87 = call <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16> %bc.84, <4 x i16> %bc.85, <4 x i16> %bc.86)
  %r = bitcast <4 x i16> %call.87 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2CMUL32/X2CMUL32S — Dual 32-bit complex multiply (Path B: 2-dest DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32>, <2 x i32>)
define dso_local i64 @test_x2cmul32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32:
; CHECK: x2cmul32
  %bc.88 = bitcast i64 %a to <2 x i32>
  %bc.89 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.88, <2 x i32> %bc.89)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x2cmul32s(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32s:
; CHECK: x2cmul32s
  %bc.90 = bitcast i64 %a to <2 x i32>
  %bc.91 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32> %bc.90, <2 x i32> %bc.91)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
