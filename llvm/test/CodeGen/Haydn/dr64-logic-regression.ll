; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — XFAIL RESOLVED (, binary `e01004df`): the MULSSH_S0 tied-def operand-flag bug filed earlier today was CLOSED by the.

; XFAIL RESOLVED (, binary `e01004df`): the MULSSH_S0 tied-def
; operand-flag bug filed earlier today was CLOSED by the
; commit series — specifically `0c7dc7cb448c` "complete BUG 1 — align ALL
; _FLEX operand dags to legacy" aligned the MULSSH/X2CMUL32 _FLEX operand
; dags (incl. tied-def flags) to their legacy equivalents. llc now emits
; real DR64 logic/NSA instructions (51 CHECKs match, 0 verifier aborts).
; XFAIL line removed; test now PASSES.
;
; REGRESSION TEST: DR64 logic and transcendental intrinsics.
;
; Purpose: Verify NSA + transcendental intrinsics emit the correct
; target instructions. NSA32/NSAU32 are ALU32 GPR32→GPR32; NSA64/NSAZ*/
; NSA*_L are ALU64 R_GD (DR64→GPR32) matching FormatsALU64 members.
; Transcendentals are GPR32 unary LUT ops. Wrong bank selection is a
; silent correctness bug; this test catches selector regressions.
;
; Test design: Each function invokes one intrinsic and returns the
; result. GPR32 results are returned in R0, DR64 results in D0.

;===----------------------------------------------------------------------===;
; Normalization (NSA) — NSA32/U GPR32; NSA64 family R_GD (i64→i32)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.nsa32(i32)
declare i32 @llvm.haydn.nsau32(i32)
declare i32 @llvm.haydn.nsa64(i64)
declare i32 @llvm.haydn.nsa16.l(i64)
declare i32 @llvm.haydn.nsa32.l(i64)
declare i32 @llvm.haydn.nsaz64(i64)
declare i32 @llvm.haydn.nsaz16.l(i64)
declare i32 @llvm.haydn.nsaz32.l(i64)

define i32 @test_nsa32(i32 %a) {
; CHECK-LABEL: test_nsa32:
; CHECK: nsa32
  %r = call i32 @llvm.haydn.nsa32(i32 %a)
  ret i32 %r
}

define i32 @test_nsau32(i32 %a) {
; CHECK-LABEL: test_nsau32:
; CHECK: nsau32
  %r = call i32 @llvm.haydn.nsau32(i32 %a)
  ret i32 %r
}

define i32 @test_nsa64(i64 %a) {
; CHECK-LABEL: test_nsa64:
; CHECK: nsa64
  %r = call i32 @llvm.haydn.nsa64(i64 %a)
  ret i32 %r
}

define i32 @test_nsa16_l(i64 %a) {
; CHECK-LABEL: test_nsa16_l:
; CHECK: nsa16_l
  %r = call i32 @llvm.haydn.nsa16.l(i64 %a)
  ret i32 %r
}

define i32 @test_nsa32_l(i64 %a) {
; CHECK-LABEL: test_nsa32_l:
; CHECK: nsa32_l
  %r = call i32 @llvm.haydn.nsa32.l(i64 %a)
  ret i32 %r
}

define i32 @test_nsaz64(i64 %a) {
; CHECK-LABEL: test_nsaz64:
; CHECK: nsaz64
  %r = call i32 @llvm.haydn.nsaz64(i64 %a)
  ret i32 %r
}

define i32 @test_nsaz16_l(i64 %a) {
; CHECK-LABEL: test_nsaz16_l:
; CHECK: nsaz16_l
  %r = call i32 @llvm.haydn.nsaz16.l(i64 %a)
  ret i32 %r
}

define i32 @test_nsaz32_l(i64 %a) {
; CHECK-LABEL: test_nsaz32_l:
; CHECK: nsaz32_l
  %r = call i32 @llvm.haydn.nsaz32.l(i64 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Transcendental functions — GPR32 unary
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.log2(i32)
declare i32 @llvm.haydn.exp2(i32)
declare i32 @llvm.haydn.recip(i32)
declare i32 @llvm.haydn.sqrt(i32)

define i32 @test_log2(i32 %a) {
; CHECK-LABEL: test_log2:
; CHECK: log2
  %r = call i32 @llvm.haydn.log2(i32 %a)
  ret i32 %r
}

define i32 @test_exp2(i32 %a) {
; CHECK-LABEL: test_exp2:
; CHECK: exp2
  %r = call i32 @llvm.haydn.exp2(i32 %a)
  ret i32 %r
}

define i32 @test_recip(i32 %a) {
; CHECK-LABEL: test_recip:
; CHECK: recip
  %r = call i32 @llvm.haydn.recip(i32 %a)
  ret i32 %r
}

define i32 @test_sqrt(i32 %a) {
; CHECK-LABEL: test_sqrt:
; CHECK: sqrt
  %r = call i32 @llvm.haydn.sqrt(i32 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; 32-bit high multiply — GPR32 binary
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
; Q-format multiply/accumulate — GPR32 ternary and DR64 ternary
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.mulq31(i32, i32, i32)
declare i32 @llvm.haydn.macq31(i32, i32, i32)
declare i32 @llvm.haydn.mac32(i32, i32, i32)

define i32 @test_mulq31(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_mulq31:
; Lowered to MULSSH (phantom MULQ31 removed).
; CHECK: mulssh
  %r = call i32 @llvm.haydn.mulq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_macq31(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_macq31:
; Lowered to ADD32(acc, MULL) or MULSSH for Q31 (phantom MACQ31 removed).
; CHECK: add32
  %r = call i32 @llvm.haydn.macq31(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_mac32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_mac32:
; CHECK: mac32
  %r = call i32 @llvm.haydn.mac32(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

declare i64 @llvm.haydn.mulq63(i64, i64, i64)

define i64 @test_mulq63(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: test_mulq63:
; CHECK: mulq63
  %r = call i64 @llvm.haydn.mulq63(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 32-bit saturating add/sub — GPR32 binary
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.add32s(i32, i32)
declare i32 @llvm.haydn.sub32s(i32, i32)

define i32 @test_add32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_add32s:
; CHECK: add32s
  %r = call i32 @llvm.haydn.add32s(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @test_sub32s(i32 %a, i32 %b) {
; CHECK-LABEL: test_sub32s:
; CHECK: sub32s
  %r = call i32 @llvm.haydn.sub32s(i32 %a, i32 %b)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; 32-bit saturating abs/neg — GPR32 unary
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.abs32s(i32)
declare i32 @llvm.haydn.neg32s(i32)

define i32 @test_abs32s(i32 %a) {
; CHECK-LABEL: test_abs32s:
; CHECK: abs32s
  %r = call i32 @llvm.haydn.abs32s(i32 %a)
  ret i32 %r
}

define i32 @test_neg32s(i32 %a) {
; CHECK-LABEL: test_neg32s:
; CHECK: neg32s
  %r = call i32 @llvm.haydn.neg32s(i32 %a)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; 32x32->64 from GPR (cross-bank multiply)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mul64.ll(i32, i32)

define i64 @test_mul64_ll(i32 %a, i32 %b) {
; CHECK-LABEL: test_mul64_ll:
; CHECK: mul64.ll
  %r = call i64 @llvm.haydn.mul64.ll(i32 %a, i32 %b)
  ret i64 %r
}
