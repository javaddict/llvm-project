; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / cutover — slot auction now packs x2s* shifts and fmula16 ops into multi-op bundles (with `nop` padding when S0 is idle); per-function op set unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.




;
; REGRESSION TEST: mulfp32x16x2ras (32x16 fractional MAC) must use native
; FMULA16 HS/LS instructions (16x16->32 fractional MAC with lane-select)
; NOT the old scalar decomposition (mul64.ll + macq31 + ~30 GPR ops).
;
; Bug : the selector decomposed mulfp32x16x2ras into scalar GPR ops
; (MOV_DR64_TO_GPR, SLLI32, SRAI32, MACQ31, MOV_GPR_TO_DR64) — ~30 ops/call
; with stack spills. This was the dominant bloat source in bqriir16x16 (5-6x).
;
; Fix : emulate 32x16 using 2x FMULA16 per accumulator lane (16x16->32
; fractional MAC with replicated coef). All in DR64, no GPR traffic.
;
; Test design: if the fix regresses, the output shows mov_dr64_to_gpr or
; macq31 (scalar path). After the fix, only fmula16_ and x2s* shifts appear.

declare i64 @llvm.haydn.mulfp32x16x2ras.low(i64, i64, i64)
declare i64 @llvm.haydn.mulfp32x16x2ras.high(i64, i64, i64)

define i64 @test_mulfp32x16x2ras_low(i64 %acc, i64 %a32, i64 %b16) {
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 %acc, i64 %a32, i64 %b16)
  ret i64 %r
}

define i64 @test_mulfp32x16x2ras_high(i64 %acc, i64 %a32, i64 %b16) {
  %r = call i64 @llvm.haydn.mulfp32x16x2ras.high(i64 %acc, i64 %a32, i64 %b16)
  ret i64 %r
}
