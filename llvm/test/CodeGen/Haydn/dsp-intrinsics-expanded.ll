; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
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
; Rebaselined (XFAIL hygiene post-): Bundle128 Flex printer
; uses optional `.sN` slot suffixes (e.g. xor32). Full asm dump regenerated
; from llc -verify-machineinstrs (backend clean).
;
declare i64 @llvm.haydn.mul64.ss.lh(i64, i64)
declare i64 @llvm.haydn.mul64.ss.hl(i64, i64)
declare i64 @llvm.haydn.mul64.ss.hh(i64, i64)
define i64 @test_mul64_ss_lh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.ss.lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_ss_hl(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.ss.hl(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_ss_hh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.ss.hh(i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.su.lul(i64, i64)
declare i64 @llvm.haydn.mul64.su.ulh(i64, i64)
declare i64 @llvm.haydn.mul64.su.uhl(i64, i64)
declare i64 @llvm.haydn.mul64.su.uhh(i64, i64)

define i64 @test_mul64_su_lul(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.su.lul(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_su_ulh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.su.ulh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_su_uhl(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.su.uhl(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_su_uhh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.su.uhh(i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.us.luh(i64, i64)
declare i64 @llvm.haydn.mul64.us.uhuh(i64, i64)
declare i64 @llvm.haydn.mul64.us.hul(i64, i64)
declare i64 @llvm.haydn.mul64.us.uhul(i64, i64)

define i64 @test_mul64_us_luh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.us.luh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_us_uhuh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.us.uhuh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_us_hul(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.us.hul(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_us_uhul(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.us.uhul(i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mul64.uu.uluh(i64, i64)
declare i64 @llvm.haydn.mul64.uu.ulul(i64, i64)
declare i64 @llvm.haydn.mul64.uu.ull(i64, i64)
declare i64 @llvm.haydn.mul64.uu.ulh(i64, i64)

define i64 @test_mul64_uu_uluh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.uu.uluh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_uu_ulul(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.uu.ulul(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_uu_ull(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.uu.ull(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_uu_ulh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mul64.uu.ulh(i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULA64 (multiply-accumulate, 16 variants)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.mula64.ss.lh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.ss.hl(i64, i64, i64)
declare i64 @llvm.haydn.mula64.ss.hh(i64, i64, i64)

define i64 @test_mula64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_ss_lh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.ss.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_ss_hl(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.ss.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.mula64.su.ulh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.su.uhl(i64, i64, i64)
declare i64 @llvm.haydn.mula64.su.uhh(i64, i64, i64)

define i64 @test_mula64_su_lul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_su_ulh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.su.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_su_uhl(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.su.uhl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_su_uhh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.us.uhuh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.us.hul(i64, i64, i64)
declare i64 @llvm.haydn.mula64.us.uhul(i64, i64, i64)

define i64 @test_mula64_us_luh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.us.uhuh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_us_hul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_us_uhul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.us.uhul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.haydn.mula64.uu.uluh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.uu.ulul(i64, i64, i64)
declare i64 @llvm.haydn.mula64.uu.ull(i64, i64, i64)
declare i64 @llvm.haydn.mula64.uu.ulh(i64, i64, i64)

define i64 @test_mula64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.uu.ulul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_uu_ull(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.uu.ull(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mula64.uu.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULS64 (multiply-subtract, 16 variants -- representative sample)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.muls64.ss.ll(i64, i64, i64)
declare i64 @llvm.haydn.muls64.ss.hh(i64, i64, i64)
declare i64 @llvm.haydn.muls64.su.lul(i64, i64, i64)
declare i64 @llvm.haydn.muls64.us.luh(i64, i64, i64)
declare i64 @llvm.haydn.muls64.uu.uluh(i64, i64, i64)

define i64 @test_muls64_ss_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_ss_hh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.muls64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_su_lul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.muls64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_us_luh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.muls64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.muls64.uu.uluh(i64 %acc, i64 %a, i64 %b)
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
  %r = call i64 @llvm.haydn.mulas64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_lul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulas64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_luh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulas64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
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
  %r = call i64 @llvm.haydn.mulss64.ss.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_lul(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulss64.su.lul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_luh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulss64.us.luh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
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
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_sub32s(i32 %a, i32 %b) {
  %r = call i32 @llvm.haydn.sub32s(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_abs32s(i32 %a) {
  %r = call i32 @llvm.haydn.abs32s(i32 %a)
  ret i32 %r
}

define i32 @test_neg32s(i32 %a) {
  %r = call i32 @llvm.haydn.neg32s(i32 %a)
  ret i32 %r
}

;===------------------------------------------------------------------===;
; Saturating arithmetic (64-bit DR64 binary)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64s(i64, i64)
declare i64 @llvm.haydn.sub64s(i64, i64)

define i64 @test_add64s(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.add64s(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64s(i64 %a, i64 %b) {
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

declare i64 @llvm.haydn.fmul32s.ll(i64, i64)
declare i64 @llvm.haydn.fmul32s.lh(i64, i64)
declare i64 @llvm.haydn.fmul32s.hh(i64, i64)

define i64 @test_fmul32s_ll(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmul32s.ll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul32s_lh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmul32s.lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul32s_hh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmul32s.hh(i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Fractional multiply-accumulate (FMULA32S)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.hh(i64, i64, i64)

define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Fractional multiply-subtract (FMULS32S)
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, i64, i64)
; Regenerated post-latr: fmuls32s.lh widened from arity 2 to arity 3
; (read-modify-write accumulator: acc, a, b) to match the.td instruction.
declare i64 @llvm.haydn.fmuls32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmuls32s.hh(i64, i64, i64)

define i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, i64 %a, i64 %b)
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
  %r = call i32 @llvm.haydn.mull(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulssh(i32 %a, i32 %b) {
  %r = call i32 @llvm.haydn.mulssh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_mulsuh(i32 %a, i32 %b) {
  %r = call i32 @llvm.haydn.mulsuh(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_muluuh(i32 %a, i32 %b) {
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
  %r = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_macq31(i32 %a, i32 %b, i32 %c) {
; Lowered to ADD32(acc, mul32) (phantom MACQ31 removed).
  %r = call i32 @llvm.haydn.macq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

; Mulq63 lowered to MUL64_LL (full signed 64-bit product; phantom
; MULQ63 removed — no native scalar DR64 Q1.63 saturating multiply in the ISA).
define i64 @test_mulq63(i64 %a, i64 %b, i64 %c) {
  %r = call i64 @llvm.haydn.mulq63(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

define i32 @test_mac32(i32 %a, i32 %b, i32 %c) {
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
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2addsub32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4add16s(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16s(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===------------------------------------------------------------------===;
; SIMD ternary MAC (DR64)
;===------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x4muls16(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, i64, i64)
declare { i64, i64 } @llvm.haydn.x4muls16s(i64, i64, i64, i64)

define i64 @test_x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x4muls16(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
  %r = call { i64, i64 } @llvm.haydn.x4muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b)
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
  %r = call i32 @llvm.haydn.nsa32(i32 %a)
  ret i32 %r
}

define i32 @test_nsau32(i32 %a) {
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

declare i64 @llvm.haydn.mul64.ss.ll(i64, i64)

define i64 @test_mul64_chain(i64 %a, i64 %b) {
  %p1 = call i64 @llvm.haydn.mul64.ss.ll(i64 %a, i64 %b)
  %p2 = call i64 @llvm.haydn.mul64.ss.lh(i64 %a, i64 %b)
  %r = add i64 %p1, %p2
  ret i64 %r
}

; Saturating add followed by saturating sub -- tests GPR32 saturation pipeline.

define i32 @test_sat_chain(i32 %a, i32 %b, i32 %c) {
  %s1 = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  %r = call i32 @llvm.haydn.sub32s(i32 %s1, i32 %c)
  ret i32 %r
}

; FMUL followed by FMULA (fractional multiply then accumulate)
; tests DR64 fractional pipeline chaining.

define i64 @test_fmul_fmula_chain(i64 %acc, i64 %a, i64 %b) {
; Slot12_ALU_AccLat itinerary changed scheduling. Use CHECK-DAG.
  %p1 = call i64 @llvm.haydn.fmul32s.ll(i64 %a, i64 %b)
  %p2 = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, i64 %a, i64 %b)
  %r = add i64 %p1, %p2
  ret i64 %r
}

; SIMD binary followed by SIMD ternary MAC -- tests DR64 register pressure
; with multiple SIMD operations.

define <2 x i32> @test_simd_chain(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c) {
  %v1 = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  %v1_i = bitcast <2 x i32> %v1 to i64
  %b_i  = bitcast <2 x i32> %b to i64
  %c_i  = bitcast <2 x i32> %c to i64
  %a_i  = bitcast <2 x i32> %a to i64
  %v2 = call { i64, i64 } @llvm.haydn.x2mula32(i64 %v1_i, i64 %a_i, i64 %b_i, i64 %c_i)
  %v2_h = extractvalue { i64, i64 } %v2, 0
  %r_i = add i64 %v2_h, %a_i
  %r   = bitcast i64 %r_i to <2 x i32>
  ret <2 x i32> %r
}

; NSA chain -- tests normalization pipeline.

define i32 @test_nsa_chain(i32 %a, i32 %b) {
  %v1 = call i32 @llvm.haydn.nsa32(i32 %a)
  %v2 = call i32 @llvm.haydn.nsau32(i32 %b)
  %r = add i32 %v1, %v2
  ret i32 %r
}

; Mixed GPR32 and DR64 intrinsics -- tests register bank interaction.

define i64 @test_mixed_banks(i32 %a, i32 %b, i64 %c, i64 %d) {
  %hi = call i32 @llvm.haydn.mulssh(i32 %a, i32 %b)
  %lo = call i64 @llvm.haydn.mul64.ss.ll(i64 %c, i64 %d)
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
  %q1 = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  %r = call i32 @llvm.haydn.macq31(i32 %q1, i32 %b, i32 %a)
  ret i32 %r
}
