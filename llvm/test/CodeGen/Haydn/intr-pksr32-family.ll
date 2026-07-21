; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s
; Updated for native DR64 shift (sll64/srl64/sra64)
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: AE_PKSR32 family — pack-shift-round with lane variants.
;
; Bug being guarded: prior to this change, only the single-accumulator
; __haydn_packsr32 (SRA64R) existed. NatureDSP IIR biquad kernels
; (bqriir32x16_df1_hifi3.c, bqriir32x32_df1_hifi3.c, bqriir32x32_df2_hifi3.c)
; use AE_PKSR32 to update the delay line and pack two int32 lanes from a 64-bit
; accumulator pair with shift+round+saturation. The existing single-operand
; intrinsic cannot represent the dual-source HH/HL/LH/LL lane pack.
;
; What this test verifies:
; Each packsr32x2_<lane> intrinsic selects to X2SRA32R (per-lane SIMD
; shift+round+sat) for each accumulator, then X2SEL32_<lane> for packing.
; All four lane variants (HH, HL, LH, LL) are exercised.
;
; What would break if the bug reappears:
; If the intrinsic defs are removed from IntrinsicsHaydn.td, this test
; fails with "undeclared intrinsic 'llvm.haydn.packsr32x2_*'".
; If the ISel decomposition regresses (drops X2SRA32R or X2SEL32), the
; MIR checks fail.
;
; Kernels that depend on these intrinsics:
; NatureDSP iir/bqriir32x16_df1_hifi3.c (4 calls)
; NatureDSP iir/bqriir32x16_df2_hifi3.c (6 calls)
; NatureDSP iir/bqriir32x32_df1_hifi3.c (8 calls)
; NatureDSP iir/bqriir32x32_df2_hifi3.c (4 calls)

declare i64 @llvm.haydn.packsr32x2.hh(i64, i64, i32)
declare i64 @llvm.haydn.packsr32x2.hl(i64, i64, i32)
declare i64 @llvm.haydn.packsr32x2.lh(i64, i64, i32)
declare i64 @llvm.haydn.packsr32x2.ll(i64, i64, i32)

; Common shift amount: 16 (typical Q17.46 -> Q1.31 delay-line pack).
define dso_local i64 @test_packsr32x2_hh(i64 %a, i64 %b) {
; MIR-LABEL: name: test_packsr32x2_hh
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_HH
; CHECK-LABEL: test_packsr32x2_hh:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_hh
  %r = call i64 @llvm.haydn.packsr32x2.hh(i64 %a, i64 %b, i32 16)
  ret i64 %r
}

define dso_local i64 @test_packsr32x2_hl(i64 %a, i64 %b) {
; MIR-LABEL: name: test_packsr32x2_hl
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_HL
; CHECK-LABEL: test_packsr32x2_hl:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_hl
  %r = call i64 @llvm.haydn.packsr32x2.hl(i64 %a, i64 %b, i32 16)
  ret i64 %r
}

define dso_local i64 @test_packsr32x2_lh(i64 %a, i64 %b) {
; MIR-LABEL: name: test_packsr32x2_lh
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_LH
; CHECK-LABEL: test_packsr32x2_lh:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_lh
  %r = call i64 @llvm.haydn.packsr32x2.lh(i64 %a, i64 %b, i32 16)
  ret i64 %r
}

define dso_local i64 @test_packsr32x2_ll(i64 %a, i64 %b) {
; MIR-LABEL: name: test_packsr32x2_ll
; MIR: X2SRA32R
; MIR: X2SRA32R
; MIR: X2SEL32_LL
; CHECK-LABEL: test_packsr32x2_ll:
; CHECK: x2sra32r
; CHECK: x2sra32r
; CHECK: x2sel32_ll
  %r = call i64 @llvm.haydn.packsr32x2.ll(i64 %a, i64 %b, i32 16)
  ret i64 %r
}

; Also keep the existing single-accumulator __haydn_packsr32 alive to prevent
; regression of the SRA64R-based pack-shift-round used by FIR/IIR output pack.
declare i32 @llvm.haydn.packsr32(i64, i32)

define dso_local i32 @test_packsr32_single(i64 %a) {
; MIR-LABEL: name: test_packsr32_single
; MIR: SRA64R
; CHECK-LABEL: test_packsr32_single:
; CHECK: sra64r
  %r = call i32 @llvm.haydn.packsr32(i64 %a, i32 16)
  ret i32 %r
}
