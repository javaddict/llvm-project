; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: golden v2_1 32X16 multiply/MAC family end-to-end ISel.
;
; Bug class (2026-08-18 v2_1 cutover): the +120 MAC RR 32X16 logicals
; gained generated defs (HaydnInstrInfoGolden.td.inc) and IR intrinsics,
; but nothing wired selection — llc -global-isel-abort=1 failed with
; "cannot select" on every llvm.haydn.<32x16> call. Accumulate forms
; additionally need the golden accumulator tie (CB-152c port law:
; (outs $rd1), (ins $rd1_in, ...), Constraints "$rd1 = $rd1_in") — an
; untied def silently risks RA splitting acc from dest (silent
; miscompile class), and a 2-op Pat on the tied def fails tblgen with
; "provided 3 operands but expected 2".
;
; Test design: one pure and one accumulating intrinsic per family shape
; (scalar-lane MUL/MULA, fractional FMUL/FMULA, saturating FMULS,
; dual X2MUL/X2MULA, complex X2CMUL, rounded-shift X2FMULRS). If the
; tie law regresses, mula/x2mula selection aborts; if defs regress to
; untied, the machine verifier or the tblgen Pat build breaks.

declare i64 @llvm.haydn.mul32x16.h0(i64, i64)
declare i64 @llvm.haydn.mula32x16.h0(i64, i64, i64)
declare i64 @llvm.haydn.fmul32x16.l3(i64, i64)
declare i64 @llvm.haydn.fmula32x16.h1(i64, i64, i64)
declare i64 @llvm.haydn.fmuls32x16.h2(i64, i64, i64)
declare i64 @llvm.haydn.mulaa32x16.h0.l1(i64, i64, i64)
declare i64 @llvm.haydn.x2mul32x16.h(i64, i64)
declare i64 @llvm.haydn.x2mula32x16.l(i64, i64, i64)
declare i64 @llvm.haydn.x2cmul32x16.h(i64, i64)
declare i64 @llvm.haydn.x2fcmula32x16rs.h(i64, i64, i64)
; Z-ops (zero-acc): golden DR_Read_Port=[rsd1,rsd2] only — NO accumulator
; input (BundleSim 0203eec found the corpus 32X16 Z-bodies wrongly reading
; rtd; index is authoritative). BINARY intrinsic + untied def; if either
; regresses to acc-reading, this select changes shape or fails.
declare i64 @llvm.haydn.mulzaa32x16.h0.l1(i64, i64)
declare i64 @llvm.haydn.fmulzss32x16.h1.l0(i64, i64)

define i64 @pure_mul(i64 %a, i64 %b) {
; CHECK-LABEL: pure_mul:
; CHECK: mul32x16.h0
  %r = call i64 @llvm.haydn.mul32x16.h0(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @acc_mula(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: acc_mula:
; CHECK: mula32x16.h0
  %r = call i64 @llvm.haydn.mula32x16.h0(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @pure_fmul_l3(i64 %a, i64 %b) {
; CHECK-LABEL: pure_fmul_l3:
; CHECK: fmul32x16.l3
  %r = call i64 @llvm.haydn.fmul32x16.l3(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @acc_fmula_h1(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: acc_fmula_h1:
; CHECK: fmula32x16.h1
  %r = call i64 @llvm.haydn.fmula32x16.h1(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sat_fmuls_h2(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: sat_fmuls_h2:
; CHECK: fmuls32x16.h2
  %r = call i64 @llvm.haydn.fmuls32x16.h2(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @dual_mulaa(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: dual_mulaa:
; CHECK: mulaa32x16.h0.l1
  %r = call i64 @llvm.haydn.mulaa32x16.h0.l1(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @x2_pure(i64 %a, i64 %b) {
; CHECK-LABEL: x2_pure:
; CHECK: x2mul32x16.h
  %r = call i64 @llvm.haydn.x2mul32x16.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @x2_acc(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: x2_acc:
; CHECK: x2mula32x16.l
  %r = call i64 @llvm.haydn.x2mula32x16.l(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @x2_cmul(i64 %a, i64 %b) {
; CHECK-LABEL: x2_cmul:
; CHECK: x2cmul32x16.h
  %r = call i64 @llvm.haydn.x2cmul32x16.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @x2_fcmla(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: x2_fcmla:
; CHECK: x2fcmula32x16rs.h
  %r = call i64 @llvm.haydn.x2fcmula32x16rs.h(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @z_mulaa(i64 %a, i64 %b) {
; CHECK-LABEL: z_mulaa:
; CHECK: mulzaa32x16.h0.l1
  %r = call i64 @llvm.haydn.mulzaa32x16.h0.l1(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @z_fmulss(i64 %a, i64 %b) {
; CHECK-LABEL: z_fmulss:
; CHECK: fmulzss32x16.h1.l0
  %r = call i64 @llvm.haydn.fmulzss32x16.h1.l0(i64 %a, i64 %b)
  ret i64 %r
}
