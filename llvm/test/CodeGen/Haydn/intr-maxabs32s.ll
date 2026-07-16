; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Un-XFAIL'd : X2ABS32S now has a real FmtALU64Unary
; encoding (0x67/0x18C) in HaydnInstrInfo.td, so AsmPrinter emits it.
;
; REGRESSION TEST: AE_MAXABS32S — per-lane saturating abs-max of two DR64 values.
;
; Bug being guarded: prior to this change, AE_MAXABS32S was a software macro
; in haydn_dsp.h that called __haydn_abs32s twice on the extracted i32 lanes of
; a single argument and recombined them, which:
; 1) used scalar GPR ops instead of the DR64 SIMD datapath
; 2) only accepted ONE argument (Xtensa AE_MAXABS32S takes TWO inputs)
; 3) had wrong semantics (Xtensa spec: per-lane MAX of saturating-abs of d0
; and d1, not the abs of a single argument)
;
; This test verifies the fused __haydn_maxabs32s intrinsic lowers to:
; X2ABS32S a -> tmp_a (per-lane saturating absolute on DR64)
; X2ABS32S b -> tmp_b
; X2MAX32 tmp_a, tmp_b -> result (per-lane max on DR64)
;
; What would break if the bug reappears:
; If the intrinsic is removed from IntrinsicsHaydn.td, this test fails with
; "undeclared intrinsic 'llvm.haydn.maxabs32s'".
; If the ISel decomposition regresses (drops an X2ABS32S or X2MAX32), the
; MIR checks fail.
; If AE_MAXABS32S in haydn_dsp.h reverts to the single-arg scalar form
; ported FFT/IIR kernels will silently use the wrong semantics.
;
; Kernels that depend on this intrinsic (48+ uses):
; NatureDSP fft/fft_cplx_stages_S2_32x32_hifi3.c (extensive magnitude
; tracking in butterfly stages)
; NatureDSP iir/math kernels (overflow detection)

declare i64 @llvm.haydn.maxabs32s(i64, i64)

define dso_local i64 @test_maxabs32s(i64 %a, i64 %b) {
; MIR-LABEL: name: test_maxabs32s
; MIR: X2ABS32S
; MIR: X2ABS32S
; MIR: X2MAX32
; CHECK-LABEL: test_maxabs32s:
; CHECK: x2abs32s
; CHECK: x2abs32s
; CHECK: x2max32
  %r = call i64 @llvm.haydn.maxabs32s(i64 %a, i64 %b)
  ret i64 %r
}
