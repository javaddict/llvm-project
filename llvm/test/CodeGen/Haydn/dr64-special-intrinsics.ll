; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Status : x4seli16 selects X4SEL16 (variable mask) / X4SELI16 (uimm4).
;
; Comprehensive DR64 special operations intrinsics test for Haydn backend.
; Tests NSA variants, transcendental functions, pack/sat operations
; complex multiply, X4 CMUL F2 variants, and 32-bit saturating operations.
;
; Categories:
; NSA normalization: nsa32, nsau32, nsa64, nsa16_l, nsa32_l, nsaz*, nsa16_l, nsaz32_l
; Transcendental: log2, exp2, recip, sqrt
; X4 pack/sat: x4sat32t16
; X4 CMUL F2 (unary DR64): x4cmul16_f2, x4cmul16s_f2
; X4 SELI16 (expected-fail: CANNOT SELECT — has its own expected-fail directive at the file header)
; 32-bit saturating: add32s, sub32s, abs32s, neg32s
; FMUL32S fractional multiply: ll, lh, hh
; FMULA32S fractional MAC: ll, lh, hh
; FMULS32S fractional MSU: ll, lh, hh
; FF2 fractional multiply (saturating + non-saturating, all lane variants)
; F2MUL fused dual MAC (IIR biquad)
; F2MULAA/F2MULSS fused dual MAC (wave 2)
; SRAI64R shift with rounding

;===----------------------------------------------------------------------===;
; NSA normalization (GPR32 unary: i32 -> i32)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.nsa32(i32)
declare i32 @llvm.haydn.nsau32(i32)
declare i32 @llvm.haydn.nsa64(i32)
declare i32 @llvm.haydn.nsa16.l(i32)
declare i32 @llvm.haydn.nsa32.l(i32)
declare i32 @llvm.haydn.nsaz64(i32)
declare i32 @llvm.haydn.nsaz16.l(i32)
declare i32 @llvm.haydn.nsaz32.l(i32)

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

define dso_local i32 @test_nsa64(i32 %a) {
; CHECK-LABEL: test_nsa64:
; CHECK: nsa64
  %r = call i32 @llvm.haydn.nsa64(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsa16_l(i32 %a) {
; CHECK-LABEL: test_nsa16_l:
; CHECK: nsa16_l
  %r = call i32 @llvm.haydn.nsa16.l(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsa32_l(i32 %a) {
; CHECK-LABEL: test_nsa32_l:
; CHECK: nsa32_l
  %r = call i32 @llvm.haydn.nsa32.l(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz64(i32 %a) {
; CHECK-LABEL: test_nsaz64:
; CHECK: nsaz64
  %r = call i32 @llvm.haydn.nsaz64(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz16_l(i32 %a) {
; CHECK-LABEL: test_nsaz16_l:
; CHECK: nsaz16_l
  %r = call i32 @llvm.haydn.nsaz16.l(i32 %a)
  ret i32 %r
}

define dso_local i32 @test_nsaz32_l(i32 %a) {
; CHECK-LABEL: test_nsaz32_l:
; CHECK: nsaz32_l
  %r = call i32 @llvm.haydn.nsaz32.l(i32 %a)
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

declare i64 @llvm.haydn.x4sat32t16(i64, i64)

define dso_local i64 @test_x4sat32t16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4sat32t16:
; CHECK: x4sat32t16
  %r = call i64 @llvm.haydn.x4sat32t16(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X4 CMUL F2 variants (unary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.x4cmul16.f2(i64)
declare i64 @llvm.haydn.x4cmul16s.f2(i64)

define dso_local i64 @test_x4cmul16_f2(i64 %a) {
; CHECK-LABEL: test_x4cmul16_f2:
; CHECK: x4cmul16_f2
  %r = call i64 @llvm.haydn.x4cmul16.f2(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_x4cmul16s_f2(i64 %a) {
; CHECK-LABEL: test_x4cmul16s_f2:
; CHECK: x4cmul16s_f2
  %r = call i64 @llvm.haydn.x4cmul16s.f2(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 CMUL F2 variants (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2cmul32.f2(i64, i64)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(i64, i64)

define dso_local i64 @test_x2cmul32_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32_f2:
; CHECK: x2cmul32_f2
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x2cmul32s_f2(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32s_f2:
; CHECK: x2cmul32s_f2
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; X4 SEL / SELI16 — variable mask → x4sel16; const uimm4 → x4seli16
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

define dso_local <4 x i16> @test_x4seli16(<4 x i16> %a, <4 x i16> %b, i32 %sel) {
; CHECK-LABEL: test_x4seli16:
; CHECK: x4sel16
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a, <4 x i16> %b, i32 %sel)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_imm(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16_imm:
; CHECK: x4seli16
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

declare i64 @llvm.haydn.fmul32s.ll(i64, i64)
declare i64 @llvm.haydn.fmul32s.lh(i64, i64)
declare i64 @llvm.haydn.fmul32s.hh(i64, i64)

define dso_local i64 @test_fmul32s_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_ll:
; CHECK: fmul32s_ll
  %r = call i64 @llvm.haydn.fmul32s.ll(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmul32s_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_lh:
; CHECK: fmul32s_lh
  %r = call i64 @llvm.haydn.fmul32s.lh(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK: fmul32s_hh
  %r = call i64 @llvm.haydn.fmul32s.hh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULA32S — Fractional 32x32->64 MAC (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.hh(i64, i64, i64)

define dso_local i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_ll:
; CHECK: fmula32s_ll
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK: fmula32s_lh
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK: fmula32s_hh
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FMULS32S — Fractional 32x32->64 MSU (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmuls32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmuls32s.hh(i64, i64, i64)

define dso_local i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_ll:
; CHECK: fmuls32s_ll
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK: fmuls32s_lh
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_hh:
; CHECK: fmuls32s_hh
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FF2 fractional multiply (saturating + rounding, all lane variants)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32rs.ll(i64, i64)
declare i64 @llvm.haydn.ff2mul32rs.lh(i64, i64)
declare i64 @llvm.haydn.ff2mul32rs.hh(i64, i64)
declare i64 @llvm.haydn.ff2mula32rs.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32rs.hh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32rs.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32rs.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32rs.hh(i64, i64, i64)

define dso_local i64 @test_ff2mul32rs_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_ll:
; CHECK: ff2mul32rs_ll
  %r = call i64 @llvm.haydn.ff2mul32rs.ll(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32rs_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_lh:
; CHECK: ff2mul32rs_lh
  %r = call i64 @llvm.haydn.ff2mul32rs.lh(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32rs_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_hh:
; CHECK: ff2mul32rs_hh
  %r = call i64 @llvm.haydn.ff2mul32rs.hh(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_ll:
; CHECK: ff2mula32rs_ll
  %r = call i64 @llvm.haydn.ff2mula32rs.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_lh:
; CHECK: ff2mula32rs_lh
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_hh:
; CHECK: ff2mula32rs_hh
  %r = call i64 @llvm.haydn.ff2mula32rs.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_ll:
; CHECK: ff2muls32rs_ll
  %r = call i64 @llvm.haydn.ff2muls32rs.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_lh:
; CHECK: ff2muls32rs_lh
  %r = call i64 @llvm.haydn.ff2muls32rs.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_hh:
; CHECK: ff2muls32rs_hh
  %r = call i64 @llvm.haydn.ff2muls32rs.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; FF2 fractional multiply (non-saturating + rounding, all lane variants)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32r.ll(i64, i64)
declare i64 @llvm.haydn.ff2mul32r.lh(i64, i64)
declare i64 @llvm.haydn.ff2mul32r.hh(i64, i64)
declare i64 @llvm.haydn.ff2mula32r.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32r.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32r.hh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32r.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32r.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32r.hh(i64, i64, i64)

define dso_local i64 @test_ff2mul32r_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_ll:
; CHECK: ff2mul32r_ll
  %r = call i64 @llvm.haydn.ff2mul32r.ll(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32r_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_lh:
; CHECK: ff2mul32r_lh
  %r = call i64 @llvm.haydn.ff2mul32r.lh(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mul32r_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_hh:
; CHECK: ff2mul32r_hh
  %r = call i64 @llvm.haydn.ff2mul32r.hh(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_ll:
; CHECK: ff2mula32r_ll
  %r = call i64 @llvm.haydn.ff2mula32r.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_lh:
; CHECK: ff2mula32r_lh
  %r = call i64 @llvm.haydn.ff2mula32r.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2mula32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_hh:
; CHECK: ff2mula32r_hh
  %r = call i64 @llvm.haydn.ff2mula32r.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_ll:
; CHECK: ff2muls32r_ll
  %r = call i64 @llvm.haydn.ff2muls32r.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_lh:
; CHECK: ff2muls32r_lh
  %r = call i64 @llvm.haydn.ff2muls32r.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_ff2muls32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_hh:
; CHECK: ff2muls32r_hh
  %r = call i64 @llvm.haydn.ff2muls32r.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; F2MULAA/F2MULSS fused dual MAC (IIR biquad, binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, i64, i64)
declare i64 @llvm.haydn.f2mulaa32rs.hllh(i64, i64, i64)
declare i64 @llvm.haydn.f2mulss32rs.hhll(i64, i64, i64)
declare i64 @llvm.haydn.f2mulss32rs.hllh(i64, i64, i64)

define dso_local i64 @test_f2mulaa32rs_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hhll:
; CHECK: f2mulaa32rs_hhll
  %r = call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_f2mulaa32rs_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulaa32rs_hllh:
; CHECK: f2mulaa32rs_hllh
  %r = call i64 @llvm.haydn.f2mulaa32rs.hllh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_f2mulss32rs_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulss32rs_hhll:
; CHECK: f2mulss32rs_hhll
  %r = call i64 @llvm.haydn.f2mulss32rs.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_f2mulss32rs_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_f2mulss32rs_hllh:
; CHECK: f2mulss32rs_hllh
  %r = call i64 @llvm.haydn.f2mulss32rs.hllh(i64 %acc, i64 %a, i64 %b)
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

declare i64 @llvm.haydn.x4fcmul16rs(i64, i64)
declare i64 @llvm.haydn.x4fcmula16rs(i64, i64, i64)
declare i64 @llvm.haydn.x4fcmul16rss(i64, i64)
declare i64 @llvm.haydn.x4fcmula16rss(i64, i64, i64)

define dso_local i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rs:
; CHECK: x4fcmul16rs
  %r = call i64 @llvm.haydn.x4fcmul16rs(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rs:
; CHECK: x4fcmula16rs
; Ternary accumulator form (acc, a, b) — reads rtd per DB.
  %r = call i64 @llvm.haydn.x4fcmula16rs(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_x4fcmul16rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rss:
; CHECK: x4fcmul16rss
  %r = call i64 @llvm.haydn.x4fcmul16rss(i64 %a, i64 %b)
  ret i64 %r
}

define dso_local i64 @test_x4fcmula16rss(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rss:
; CHECK: x4fcmula16rss
; Ternary accumulator form.
  %r = call i64 @llvm.haydn.x4fcmula16rss(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2CMUL32/X2CMUL32S — Dual 32-bit complex multiply (Path B: 2-dest DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2cmul32(i64, i64)
declare { i64, i64 } @llvm.haydn.x2cmul32s(i64, i64)

define dso_local i64 @test_x2cmul32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32:
; CHECK: x2cmul32
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x2cmul32s(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2cmul32s:
; CHECK: x2cmul32s
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
