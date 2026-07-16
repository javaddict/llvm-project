; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: DR64 MUL/MAC intrinsics — sign combo coverage.
;
; Purpose: Verify that the 64-bit multiply (MUL64), multiply-accumulate
; (MULA64), multiply-subtract (MULS64), multiply-accumulate-subtract
; (MULAS64), and multiply-subtract-subtract (MULSS64) intrinsics emit
; the correct instruction for each sign-combination variant (SS, SU
; US, UU) and lane pair (LL, LH, HL, HH).
;
; Why this test: The selector maps 16 sign combos x 4 lane pairs per
; MUL/MULA/MULS/MULAS/MULSS family = 320 intrinsic-to-instruction
; mappings. A typo in any mapping silently produces the wrong instruction.
; This test verifies one representative from each sign combo per family.
;
; Test design: One function per sign-combo representative, returning the
; result to ensure the instruction survives to assembly output.

;===----------------------------------------------------------------------===;
; MUL64 — one per sign combo (LL lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.ss.ll(i64, i64)
declare i64 @llvm.haydn.mul64.su.lul(i64, i64)
declare i64 @llvm.haydn.mul64.us.luh(i64, i64)
declare i64 @llvm.haydn.mul64.uu.uluh(i64, i64)

define i64 @test_mul64_ss_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_ll:
; CHECK: mul64.ll
  %r = call i64 @llvm.haydn.mul64.ss.ll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_su_lul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_lul:
; CHECK: mul64.lul
  %r = call i64 @llvm.haydn.mul64.su.lul(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_us_luh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_luh:
; CHECK: mul64.luh
  %r = call i64 @llvm.haydn.mul64.us.luh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mul64_uu_uluh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_uluh:
; CHECK: mul64.uluh
  %r = call i64 @llvm.haydn.mul64.uu.uluh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 — one per sign combo (HH lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.ss.hh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.su.uhh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.us.uhuh(i64, i64, i64)
declare i64 @llvm.haydn.mula64.uu.uluh(i64, i64, i64)

define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hh:
; CHECK: mula64.hh
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhh:
; CHECK: mula64.uhh
  %r = call i64 @llvm.haydn.mula64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhuh:
; CHECK: mula64.uhuh
  %r = call i64 @llvm.haydn.mula64.us.uhuh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mula64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_uluh:
; CHECK: mula64.uluh
  %r = call i64 @llvm.haydn.mula64.uu.uluh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULS64 — one per sign combo (LH lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.muls64.ss.lh(i64, i64, i64)
declare i64 @llvm.haydn.muls64.su.ulh(i64, i64, i64)
declare i64 @llvm.haydn.muls64.us.hul(i64, i64, i64)
declare i64 @llvm.haydn.muls64.uu.ulh(i64, i64, i64)

define i64 @test_muls64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_lh:
; CHECK: muls64.lh
  %r = call i64 @llvm.haydn.muls64.ss.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_ulh:
; CHECK: muls64.ulh
  %r = call i64 @llvm.haydn.muls64.su.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_hul:
; CHECK: muls64.hul
  %r = call i64 @llvm.haydn.muls64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_muls64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_ulh:
; CHECK: muls64.ulh
  %r = call i64 @llvm.haydn.muls64.uu.ulh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULAS64 — one per sign combo (HL lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulas64.ss.hl(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.su.uhl(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.us.hul(i64, i64, i64)
declare i64 @llvm.haydn.mulas64.uu.ull(i64, i64, i64)

define i64 @test_mulas64_ss_hl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_ss_hl:
; CHECK: mulas64_hl
  %r = call i64 @llvm.haydn.mulas64.ss.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_uhl:
; CHECK: mulas64_uhl
  %r = call i64 @llvm.haydn.mulas64.su.uhl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_hul:
; CHECK: mulas64_hul
  %r = call i64 @llvm.haydn.mulas64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_ull:
; CHECK: mulas64_ull
  %r = call i64 @llvm.haydn.mulas64.uu.ull(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULSS64 — one per sign combo (HH lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulss64.ss.hh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.su.uhh(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.us.uhul(i64, i64, i64)
declare i64 @llvm.haydn.mulss64.uu.ulul(i64, i64, i64)

define i64 @test_mulss64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_ss_hh:
; CHECK: mulss64_hh
  %r = call i64 @llvm.haydn.mulss64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_uhh:
; CHECK: mulss64_uhh
  %r = call i64 @llvm.haydn.mulss64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_uhul:
; CHECK: mulss64_uhul
  %r = call i64 @llvm.haydn.mulss64.us.uhul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_ulul:
; CHECK: mulss64_ulul
  %r = call i64 @llvm.haydn.mulss64.uu.ulul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply (FMUL32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul32s.ll(i64, i64)
declare i64 @llvm.haydn.fmul32s.lh(i64, i64)
declare i64 @llvm.haydn.fmul32s.hh(i64, i64)

define i64 @test_fmul32s_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_ll:
; CHECK: fmul32s_ll
  %r = call i64 @llvm.haydn.fmul32s.ll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul32s_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_lh:
; CHECK: fmul32s_lh
  %r = call i64 @llvm.haydn.fmul32s.lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK: fmul32s_hh
  %r = call i64 @llvm.haydn.fmul32s.hh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply-accumulate (FMULA32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmula32s.hh(i64, i64, i64)

define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_ll:
; CHECK: fmula32s_ll
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK: fmula32s_lh
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK: fmula32s_hh
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply-subtract (FMULS32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, i64, i64)
; Regenerated post-latr: llvm.haydn.fmuls32s.lh widened from arity 2 to
; arity 3 (read-modify-write accumulator: acc, a, b) — matches the.td
; fmuls32s_lh which now takes 3 DR64 operands. Update only; selector still
; emits `fmuls32s_lh`. Do NOT revert to 2 args (the verifier rejects it:
; "Callsite was not defined with variable arguments!").
declare i64 @llvm.haydn.fmuls32s.lh(i64, i64, i64)
declare i64 @llvm.haydn.fmuls32s.hh(i64, i64, i64)

define i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_ll:
; CHECK: fmuls32s_ll
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK: fmuls32s_lh
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_hh:
; CHECK: fmuls32s_hh
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}
