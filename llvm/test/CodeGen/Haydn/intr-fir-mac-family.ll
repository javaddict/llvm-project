; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

;
; REGRESSION TEST: llvm.haydn.mulaa32s.fir.{hh,hl} (renamed from
; llvm.haydn.mulafd32x16x2.fir.{hh,hl} by A2)
; llvm.haydn.mulfq16x2.fir.{3,1}, llvm.haydn.mulafq16x2.fir.{3,1}.
;
; Context: 7 degraded NatureDSP FIR kernels (bkfir16x16, firdec32x32
; convol16x16, blms16x16, cxfir16x16, bkfira16x16, firinterp16x16) all use
; HiFi3 dual/quad-output FIR MAC intrinsics (AE_MUL{,A}FD32X16X2_FIR_HH/HL
; AE_MUL{,A}FQ16X2_FIR_3/1). Haydn has no native dual/quad-output FIR MAC
; (see ISA-09-fir-simd-mac-gap.md), so each LLVM intrinsic is one MAC step
; the kernel caller issues per output accumulator lane (see
; fir-mac-family-decomposition.md).
;
; RENAME (A2): llvm.haydn.mulafd32x16x2.fir.{hh,hl} were renamed to
; llvm.haydn.mulaa32s.fir.{hh,hl}. The old name claimed "32x16 dual" but the

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_mulaa32s_fir_hh:
; CHECK-LABEL: test_mulaa32s_fir_hl:
; CHECK-LABEL: test_mulfq16x2_fir_3:
; CHECK-LABEL: test_mulfq16x2_fir_1:
; CHECK-LABEL: test_mulafq16x2_fir_3:
; CHECK-LABEL: test_mulafq16x2_fir_1:
; CHECK-LABEL: test_fir_chain_hh_hl:
; CHECK-LABEL: test_fir_chain_init_accumulate:
; CHECK: {{.}}

declare i64 @llvm.haydn.mulaa32s.fir.hh(i64, i64, i64)
declare i64 @llvm.haydn.mulaa32s.fir.hl(i64, i64, i64)
declare i64 @llvm.haydn.mulfq16x2.fir.3(i64, i64, i64)
declare i64 @llvm.haydn.mulfq16x2.fir.1(i64, i64, i64)
declare i64 @llvm.haydn.mulafq16x2.fir.3(i64, i64, i64)
declare i64 @llvm.haydn.mulafq16x2.fir.1(i64, i64, i64)

;===------------------------------------------------------------------===;
; MULAA32S_FIR_HH (was MULAFD32X16X2_FIR_HH) — 32x32 fractional MAC, HH lanes.
; Lowers to: coef-widen (mov_dr64_to_gpr + srai32 + mov_gpr_to_dr64) +
; FMULA32S_HH.
; Unblocks: bkfir16x16, firdec32x32 (dual-output 32x16 FIR MAC step).
;===------------------------------------------------------------------===;

define i64 @test_mulaa32s_fir_hh(i64 %acc, i64 %a, i64 %b) {
; Coef-widen (A3 + SEXT-fold defeat): spill+extract hi lane
; srai32 sext to Q1.31, then repack via MOV_GPR_TO_DR64 CoefW, R0, CoefQ31
; (R0 != CoefQ31 defeats the sext fold). The coef path must NOT contain
; sext32t64 — that mnemonic means the sext fold fired and rsd2[63:32] is a
; sign mask (wrong-code). The two st32 + ld64 are the mov_gpr_to_dr64
; SP-relative expansion storing R0 at [sp,0] (low=0) and CoefQ31 at [sp,4]
; (high=coef).
  %r = call i64 @llvm.haydn.mulaa32s.fir.hh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULAA32S_FIR_HL (was MULAFD32X16X2_FIR_HL) — 32x32 fractional MAC, LH lanes.
; Lowers to: coef-widen + FMULA32S_LH.
;===------------------------------------------------------------------===;

define i64 @test_mulaa32s_fir_hl(i64 %acc, i64 %a, i64 %b) {
; Coef-widen (A3 + SEXT-fold defeat): same sequence as _hh but the
; MAC reads rsd1[31:00]*rsd2[63:32] (low data * high coef). rsd2[31:00]=0
; (R0) is dead for LH. Must NOT contain sext32t64.
  %r = call i64 @llvm.haydn.mulaa32s.fir.hl(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULFQ16X2_FIR_3 — 16x16 fractional multiply init, lane-pair 0/1.
; Non-accumulating: rtd[63:32] = a_l0 * b_l0.
; Lowers to: FMUL16_HS00 (no coef-widen — 16x16 op, width already matches).
;===------------------------------------------------------------------===;

define i64 @test_mulfq16x2_fir_3(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulfq16x2.fir.3(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULFQ16X2_FIR_1 — 16x16 fractional multiply init, lane-pair 2/3.
; Lowers to: FMUL16_HS22.
;===------------------------------------------------------------------===;

define i64 @test_mulfq16x2_fir_1(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulfq16x2.fir.1(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULAFQ16X2_FIR_3 — 16x16 fractional MAC, lane-pair 1+0.
; acc += a_l1*b_l1 + a_l0*b_l0 (saturating Q1.31).
; Lowers to: FMULAA16_HS_11_00.
; Unblocks: blms16x16, convol16x16 (quad-MAC FIR accumulate step).
;===------------------------------------------------------------------===;

define i64 @test_mulafq16x2_fir_3(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulafq16x2.fir.3(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; MULAFQ16X2_FIR_1 — 16x16 fractional MAC, lane-pair 3+2.
; Lowers to: FMULAA16_HS_33_22.
;===------------------------------------------------------------------===;

define i64 @test_mulafq16x2_fir_1(i64 %acc, i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.mulafq16x2.fir.1(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

;===------------------------------------------------------------------===;
; Chained use — verify the decomposition composes when the result feeds
; another intrinsic of the same family (no DCE, no copy elision breaking
; the chain). Two coef-widens + two MAC ops total.
;===------------------------------------------------------------------===;

define i64 @test_fir_chain_hh_hl(i64 %acc0, i64 %a, i64 %b) {
  %a1 = call i64 @llvm.haydn.mulaa32s.fir.hh(i64 %acc0, i64 %a, i64 %b)
  %a2 = call i64 @llvm.haydn.mulaa32s.fir.hl(i64 %a1, i64 %a, i64 %b)
  ret i64 %a2
}

define i64 @test_fir_chain_init_accumulate(i64 %acc0, i64 %a, i64 %b) {
  %a1 = call i64 @llvm.haydn.mulfq16x2.fir.3(i64 %acc0, i64 %a, i64 %b)
  %a2 = call i64 @llvm.haydn.mulafq16x2.fir.3(i64 %a1, i64 %a, i64 %b)
  ret i64 %a2
}
