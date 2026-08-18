; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — DR64 MUL/MAC intrinsics — sign combo coverage.

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

declare i64 @llvm.haydn.mul64.ss.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.su.lul(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.us.luh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mul64.uu.uluh(<2 x i32>, <2 x i32>)
define i64 @test_mul64_ss_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_ss_ll:
; CHECK: mul64.ll
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.ss.ll(<2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

define i64 @test_mul64_su_lul(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_su_lul:
; CHECK: mul64.lul
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.su.lul(<2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

define i64 @test_mul64_us_luh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_us_luh:
; CHECK: mul64.luh
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.us.luh(<2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

define i64 @test_mul64_uu_uluh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mul64_uu_uluh:
; CHECK: mul64.uluh
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mul64.uu.uluh(<2 x i32> %bc.7, <2 x i32> %bc.8)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULA64 — one per sign combo (HH lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mula64.ss.hh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.su.uhh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.us.uhuh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.mula64.uu.uluh(i64, <2 x i32>, <2 x i32>)
define i64 @test_mula64_ss_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_ss_hh:
; CHECK: mula64.hh
  %bc.9 = bitcast i64 %a to <2 x i32>
  %bc.10 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.hh(i64 %acc, <2 x i32> %bc.9, <2 x i32> %bc.10)
  ret i64 %r
}

define i64 @test_mula64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_su_uhh:
; CHECK: mula64.uhh
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.su.uhh(i64 %acc, <2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}

define i64 @test_mula64_us_uhuh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_us_uhuh:
; CHECK: mula64.uhuh
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.us.uhuh(i64 %acc, <2 x i32> %bc.13, <2 x i32> %bc.14)
  ret i64 %r
}

define i64 @test_mula64_uu_uluh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mula64_uu_uluh:
; CHECK: mula64.uluh
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.uu.uluh(i64 %acc, <2 x i32> %bc.15, <2 x i32> %bc.16)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; MULS64 — one per sign combo (LH lane variant)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.muls64.ss.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.su.ulh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.us.hul(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.uu.ulh(i64, <2 x i32>, <2 x i32>)
define i64 @test_muls64_ss_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_ss_lh:
; CHECK: muls64.lh
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.lh(i64 %acc, <2 x i32> %bc.17, <2 x i32> %bc.18)
  ret i64 %r
}

define i64 @test_muls64_su_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_su_ulh:
; CHECK: muls64.ulh
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.su.ulh(i64 %acc, <2 x i32> %bc.19, <2 x i32> %bc.20)
  ret i64 %r
}

define i64 @test_muls64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_us_hul:
; CHECK: muls64.hul
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.us.hul(i64 %acc, <2 x i32> %bc.21, <2 x i32> %bc.22)
  ret i64 %r
}

define i64 @test_muls64_uu_ulh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_muls64_uu_ulh:
; CHECK: muls64.ulh
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.uu.ulh(i64 %acc, <2 x i32> %bc.23, <2 x i32> %bc.24)
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
; CHECK: mulas64.hl
  %r = call i64 @llvm.haydn.mulas64.ss.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_su_uhl(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_su_uhl:
; CHECK: mulas64.uhl
  %r = call i64 @llvm.haydn.mulas64.su.uhl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_us_hul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_us_hul:
; CHECK: mulas64.hul
  %r = call i64 @llvm.haydn.mulas64.us.hul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulas64_uu_ull(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulas64_uu_ull:
; CHECK: mulas64.ull
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
; CHECK: mulss64.hh
  %r = call i64 @llvm.haydn.mulss64.ss.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_su_uhh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_su_uhh:
; CHECK: mulss64.uhh
  %r = call i64 @llvm.haydn.mulss64.su.uhh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_us_uhul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_us_uhul:
; CHECK: mulss64.uhul
  %r = call i64 @llvm.haydn.mulss64.us.uhul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss64_uu_ulul(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss64_uu_ulul:
; CHECK: mulss64.ulul
  %r = call i64 @llvm.haydn.mulss64.uu.ulul(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply (FMUL32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmul32s.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.lh(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmul32s.hh(<2 x i32>, <2 x i32>)
define i64 @test_fmul32s_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_ll:
; CHECK: fmul32s_ll
  %bc.25 = bitcast i64 %a to <2 x i32>
  %bc.26 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.ll(<2 x i32> %bc.25, <2 x i32> %bc.26)
  ret i64 %r
}

define i64 @test_fmul32s_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_lh:
; CHECK: fmul32s_lh
  %bc.27 = bitcast i64 %a to <2 x i32>
  %bc.28 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.lh(<2 x i32> %bc.27, <2 x i32> %bc.28)
  ret i64 %r
}

define i64 @test_fmul32s_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_fmul32s_hh:
; CHECK: fmul32s_hh
  %bc.29 = bitcast i64 %a to <2 x i32>
  %bc.30 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmul32s.hh(<2 x i32> %bc.29, <2 x i32> %bc.30)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply-accumulate (FMULA32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmula32s.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmula32s.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_fmula32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_ll:
; CHECK: fmula32s_ll
  %bc.31 = bitcast i64 %a to <2 x i32>
  %bc.32 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.ll(i64 %acc, <2 x i32> %bc.31, <2 x i32> %bc.32)
  ret i64 %r
}

define i64 @test_fmula32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_lh:
; CHECK: fmula32s_lh
  %bc.33 = bitcast i64 %a to <2 x i32>
  %bc.34 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.lh(i64 %acc, <2 x i32> %bc.33, <2 x i32> %bc.34)
  ret i64 %r
}

define i64 @test_fmula32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmula32s_hh:
; CHECK: fmula32s_hh
  %bc.35 = bitcast i64 %a to <2 x i32>
  %bc.36 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmula32s.hh(i64 %acc, <2 x i32> %bc.35, <2 x i32> %bc.36)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Fractional multiply-subtract (FMULS32S) — all lane variants
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.fmuls32s.ll(i64, <2 x i32>, <2 x i32>)
; Regenerated post-latr: llvm.haydn.fmuls32s.lh widened from arity 2 to
; arity 3 (read-modify-write accumulator: acc, a, b) — matches the.td
; fmuls32s_lh which now takes 3 DR64 operands. Update only; selector still
; emits `fmuls32s_lh`. Do NOT revert to 2 args (the verifier rejects it:
; "Callsite was not defined with variable arguments!").
declare i64 @llvm.haydn.fmuls32s.lh(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.fmuls32s.hh(i64, <2 x i32>, <2 x i32>)
define i64 @test_fmuls32s_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_ll:
; CHECK: fmuls32s_ll
  %bc.37 = bitcast i64 %a to <2 x i32>
  %bc.38 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.ll(i64 %acc, <2 x i32> %bc.37, <2 x i32> %bc.38)
  ret i64 %r
}

define i64 @test_fmuls32s_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_lh:
; CHECK: fmuls32s_lh
  %bc.39 = bitcast i64 %a to <2 x i32>
  %bc.40 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.lh(i64 %acc, <2 x i32> %bc.39, <2 x i32> %bc.40)
  ret i64 %r
}

define i64 @test_fmuls32s_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_fmuls32s_hh:
; CHECK: fmuls32s_hh
  %bc.41 = bitcast i64 %a to <2 x i32>
  %bc.42 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.fmuls32s.hh(i64 %acc, <2 x i32> %bc.41, <2 x i32> %bc.42)
  ret i64 %r
}
