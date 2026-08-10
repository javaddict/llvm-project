; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: GPR32 -> DR64 sign-extension feeding a DR64 op must NOT
; round-trip through the stack (Phase 2 of next-phase-fixes-plan-2026-06-14).
;
; Gap class: SEXT / GPR->DR64 data movement.
;
; Bug / open gap: every i32 -> i64 sign-extension that needs to land in a DR64
; register currently materializes via a stack spill:
; sra32; subi32 sp,sp,8; st32; st32; ld64; addi32 sp,sp,8
; (5+ bundles per widen). This dominates FFT butterflies, vec_max
; cplx2cplx_mult32x32, and both FFT kernels — ~5 wasted bundles per MAC
; operand. There is no direct GPR32 -> DR64 sign-extending move.
;
; Target state (drops XFAIL): the sext feeds the DR64 operand directly (a
; sign-extending move / fold), with NO st32 and NO ld64 between the load and
; the DR64 op in the function body.
;
; Test design: load an i32, sign-extend it to i64, feed it into the DR64
; x2mul32 intrinsic (which is known to select to the native x2mul32
; instruction — see simd.ll). The CHECK-NOTs span the whole function so any
; stack round-trip fails the test.
;
; When the GPR32->DR64 sext fix lands (HaydnInstructionSelector.cpp and/or
; HaydnPostSelectOptimize.cpp, possibly a new MOV_SEXT64 pseudo), drop the
; XFAIL above and confirm the CHECKs match. Do NOT relax the CHECK-NOTs to
; match the spill-emitting codegen — fix the selector instead.

declare { i64, i64 } @llvm.haydn.x2mul32(<2 x i32>, <2 x i32>)
define i64 @ii_gpr_dr64_sext_no_spill(ptr %p, i64 %acc) {
; CHECK-LABEL: ii_gpr_dr64_sext_no_spill:
; CHECK:       x2mul32
; CHECK-NOT:   s_sw_
; CHECK-NOT:   d_ldw_
entry:
  %a32 = load i32, ptr %p
  %a64 = sext i32 %a32 to i64
  ; The sext operand must move directly GPR32 -> DR64; if it round-trips
  ; through the stack, st32+ld64 appear and this test fails (as it should
  ; today, hence XFAIL).
  ; (Path B): x2mul32 is 2-dest non-accum — args (src1, src2), returns {i64,i64}.
  %bc.1 = bitcast i64 %a64 to <2 x i32>
  %bc.2 = bitcast i64 %a64 to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}
