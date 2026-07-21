; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; FF2 Fractional MAC intrinsic test for Haydn backend.
; Covers ALL 18 FF2 intrinsics (each in ll/lh/hh lane variants):
; ff2mul32r.{ll,lh,hh} (non-saturating single-product mul, FmtALU64 2-src)
; ff2mul32rs.{ll,lh,hh} (saturating single-product mul, FmtALU64 2-src)
; ff2mula32rs.{ll,lh,hh} (saturating single-product MAC, FmtALU64Acc 3-src tied-def)
; ff2muls32rs.{ll,lh,hh} (saturating single-product MSUB, FmtALU64Acc 3-src tied-def)
; ff2mula32r.{ll,lh,hh} (non-saturating single-product MAC, FmtALU64Acc 3-src tied-def)
; ff2muls32r.{ll,lh,hh} (non-saturating single-product MSUB, FmtALU64Acc 3-src tied-def)
;
; The FF2* intrinsic family maps to the FF2* instruction family (single
; product per lane) per haydn_instruction_db.json — NOT the dual-product
; F2MULAA32RS_*/F2MULSS32RS_*/F2MULAA32R_*/F2MULSS32R_* family. The saturating
; _RS mul family was corrected first (m6-codex-destructive-acc-lanemap.md §2);
; extended the fix to the saturating subtract (_RS sub) and the entire
; non-saturating _R family (mul, MAC, sub) — all 9 non-saturating + 3
; saturating-subtract variants were previously routed to dual-product opcodes.
; reconciled the ACCUMULATOR arity end-to-end: the 9 accumulator ops
; (ff2mula32r.*, ff2muls32r.*, ff2muls32rs.*) are now ternary (acc, a, b) with
; FmtALU64Acc tied-def target opcodes and selectAccMAC selection — they read
; rtd as the accumulator (DB slot-1 DR_Read_Port = [rsd1, rsd2, rtd]). The
; 9 accumulator functions below were previously split into the XFAIL'd
; companion ff2-fractional-mac-accumulator-bug.ll (which silently failed at
; IR verification because the test declared ternary arity while the code was
; still binary — see); that file is deleted, the functions are merged
; back here, and they are now live regression tests.
; Lane mapping:
; ff2*32r_ll -> FF2*32R_LL (low*low, single product)
; ff2*32r_lh -> FF2*32R_LH (low*high, single product)
; ff2*32r_hh -> FF2*32R_HH (high*high, single product)
; ff2*32rs_ll -> FF2*32RS_LL (low*low, single product, saturating)

;===------------------------------------------------------------------===;
; FF2MUL32RS — saturating fractional multiply with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32rs.ll(i64, i64)
declare i64 @llvm.haydn.ff2mul32rs.lh(i64, i64)
declare i64 @llvm.haydn.ff2mul32rs.hh(i64, i64)

define i64 @test_ff2mul32rs_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_ll:
; CHECK: ff2mul32rs_ll
  %r = call i64 @llvm.haydn.ff2mul32rs.ll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mul32rs_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_lh:
; CHECK: ff2mul32rs_lh
  %r = call i64 @llvm.haydn.ff2mul32rs.lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mul32rs_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32rs_hh:
; CHECK: ff2mul32rs_hh
  %r = call i64 @llvm.haydn.ff2mul32rs.hh(i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; FF2MULA32RS — saturating fractional multiply-accumulate with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mula32rs.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32rs.hh(i64, i64, i64)

define i64 @test_ff2mula32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_ll:
; CHECK: ff2mula32rs_ll
  %r = call i64 @llvm.haydn.ff2mula32rs.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_lh:
; CHECK: ff2mula32rs_lh
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mula32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32rs_hh:
; CHECK: ff2mula32rs_hh
  %r = call i64 @llvm.haydn.ff2mula32rs.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; FF2MULS32RS — saturating fractional multiply-subtract with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2muls32rs.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32rs.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32rs.hh(i64, i64, i64)

define i64 @test_ff2muls32rs_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_ll:
; REGRESSION (/): ff2muls32rs.ll must route to the SINGLE-product
; FF2MULS32RS_LL (rtd = rtd - sat(rnd(rsd1[31:00]*rsd2[31:00]))), NOT the
; dual-lane F2MULSS32RS_HHLL (which subtracts HH*HH + LL*LL). The intrinsic
; is ternary (acc, a, b) and reads rtd as the tied-def accumulator. If this
; regresses to f2mulss32rs_hhll or the accumulator is dropped, the intrinsic
; computes a wrong result.
; CHECK: ff2muls32rs_ll
  %r = call i64 @llvm.haydn.ff2muls32rs.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2muls32rs_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_lh:
; CHECK: ff2muls32rs_lh
  %r = call i64 @llvm.haydn.ff2muls32rs.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2muls32rs_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32rs_hh:
; CHECK: ff2muls32rs_hh
  %r = call i64 @llvm.haydn.ff2muls32rs.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; FF2MUL32R — non-saturating fractional multiply with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mul32r.ll(i64, i64)
declare i64 @llvm.haydn.ff2mul32r.lh(i64, i64)
declare i64 @llvm.haydn.ff2mul32r.hh(i64, i64)

define i64 @test_ff2mul32r_ll(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_ll:
; REGRESSION : ff2mul32r.ll must route to the SINGLE-product
; FF2MUL32R_LL (rtd = rnd(rsd1[31:00]*rsd2[31:00])), NOT the dual-lane
; F2MULAA32R_HHLL (which sums HH*HH + LL*LL). If this regresses to
; f2mulaa32r_hhll the intrinsic computes a wrong (dual-summed) result.
; CHECK: ff2mul32r_ll
  %r = call i64 @llvm.haydn.ff2mul32r.ll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mul32r_lh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_lh:
; CHECK: ff2mul32r_lh
  %r = call i64 @llvm.haydn.ff2mul32r.lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mul32r_hh(i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mul32r_hh:
; CHECK: ff2mul32r_hh
  %r = call i64 @llvm.haydn.ff2mul32r.hh(i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; FF2MULA32R — non-saturating single-product fractional MAC with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2mula32r.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32r.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2mula32r.hh(i64, i64, i64)

define i64 @test_ff2mula32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_ll:
; REGRESSION (/): ff2mula32r.ll must route to the SINGLE-product
; FF2MULA32R_LL (rtd = rtd + rnd(rsd1[31:00]*rsd2[31:00])), NOT the dual-lane
; F2MULAA32R_HHLL (which sums HH*HH + LL*LL). The intrinsic is ternary
; (acc, a, b) and reads rtd as the tied-def accumulator (FmtALU64Acc).
; If this regresses — either to f2mulaa32r_hhll, or by dropping the acc
; operand (the pre- latent miscompute) — the intrinsic computes a wrong
; result. Both defects were tracked in the deleted companion file
; ff2-fractional-mac-accumulator-bug.ll; this case is now a live regression
; test for the full arity reconciliation.
; CHECK: ff2mula32r_ll
  %r = call i64 @llvm.haydn.ff2mula32r.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mula32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_lh:
; CHECK: ff2mula32r_lh
  %r = call i64 @llvm.haydn.ff2mula32r.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2mula32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2mula32r_hh:
; CHECK: ff2mula32r_hh
  %r = call i64 @llvm.haydn.ff2mula32r.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; FF2MULS32R — non-saturating single-product fractional MSUB with rounding
;===------------------------------------------------------------------===;

declare i64 @llvm.haydn.ff2muls32r.ll(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32r.lh(i64, i64, i64)
declare i64 @llvm.haydn.ff2muls32r.hh(i64, i64, i64)

define i64 @test_ff2muls32r_ll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_ll:
; REGRESSION (/): ff2muls32r.ll must route to the SINGLE-product
; FF2MULS32R_LL (rtd = rtd - rnd(rsd1[31:00]*rsd2[31:00])), NOT the dual-lane
; F2MULSS32R_HHLL (which subtracts HH*HH + LL*LL). Ternary (acc, a, b)
; tied-def accumulator. Same regression contract as test_ff2mula32r_ll above.
; CHECK: ff2muls32r_ll
  %r = call i64 @llvm.haydn.ff2muls32r.ll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2muls32r_lh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_lh:
; CHECK: ff2muls32r_lh
  %r = call i64 @llvm.haydn.ff2muls32r.lh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_ff2muls32r_hh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_ff2muls32r_hh:
; CHECK: ff2muls32r_hh
  %r = call i64 @llvm.haydn.ff2muls32r.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

