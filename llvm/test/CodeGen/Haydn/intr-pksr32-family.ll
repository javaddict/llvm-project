; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; AE_PKSR-style dual pack is a *composite* (not a golden PACKSR* encoding).
; haydn_dsp.h expands it as:
;   tmp_a = X2SRA32R a, sh
;   tmp_b = X2SRA32R b, sh
;   result = X2SEL32_<lane> tmp_a, tmp_b
; Single-acc pack is DB SRA64R (public haydn_sra64r / haydn_packsr32 wrapper).
;
; This test exercises the real DB ops the composite expands into — not invented
; llvm.haydn.packsr32x2.* intrinsics.

declare <2 x i32> @llvm.haydn.x2sra32r(<2 x i32>, i32)
; IR names: llvm.haydn.x2sel32.hh (from int_haydn_x2sel32_hh)
declare <2 x i32> @llvm.haydn.x2sel32.hh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.hl(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sel32.ll(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.sra64r(i64, i32)

define dso_local <2 x i32> @test_packsr_compose_hh(<2 x i32> %a, <2 x i32> %b) {
; MIR-LABEL: name: test_packsr_compose_hh
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_HH
; CHECK-LABEL: test_packsr_compose_hh:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_hh
  %ta = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 16)
  %tb = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %b, i32 16)
  %r = call <2 x i32> @llvm.haydn.x2sel32.hh(<2 x i32> %ta, <2 x i32> %tb)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_packsr_compose_hl(<2 x i32> %a, <2 x i32> %b) {
; MIR-LABEL: name: test_packsr_compose_hl
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_HL
; CHECK-LABEL: test_packsr_compose_hl:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_hl
  %ta = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 16)
  %tb = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %b, i32 16)
  %r = call <2 x i32> @llvm.haydn.x2sel32.hl(<2 x i32> %ta, <2 x i32> %tb)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_packsr_compose_lh(<2 x i32> %a, <2 x i32> %b) {
; MIR-LABEL: name: test_packsr_compose_lh
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_LH
; CHECK-LABEL: test_packsr_compose_lh:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_lh
  %ta = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 16)
  %tb = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %b, i32 16)
  %r = call <2 x i32> @llvm.haydn.x2sel32.lh(<2 x i32> %ta, <2 x i32> %tb)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_packsr_compose_ll(<2 x i32> %a, <2 x i32> %b) {
; MIR-LABEL: name: test_packsr_compose_ll
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_LL
; CHECK-LABEL: test_packsr_compose_ll:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_ll
  %ta = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 16)
  %tb = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %b, i32 16)
  %r = call <2 x i32> @llvm.haydn.x2sel32.ll(<2 x i32> %ta, <2 x i32> %tb)
  ret <2 x i32> %r
}

define dso_local i64 @test_sra64r_pack(i64 %a) {
; MIR-LABEL: name: test_sra64r_pack
; MIR: SRA64R
; CHECK-LABEL: test_sra64r_pack:
; CHECK: sra64r
  %r = call i64 @llvm.haydn.sra64r(i64 %a, i32 16)
  ret i64 %r
}
