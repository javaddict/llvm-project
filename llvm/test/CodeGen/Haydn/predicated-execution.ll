; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s

; Role: MIR — Predicated execution patterns using SFR flags.

; REGRESSION TEST: Predicated execution patterns using SFR flags.
;
; Haydn's predication model:
; 1. Compare instructions (SEQ64, SLT64, SLE64 for scalar DR64;
; X2SEQ32, X2SLT32, X2SLE32 for SIMD X2; X4SEQ16, X4SLT16, X4SLE16 for SIMD X4)
; set per-lane SFR flags.
; 2. Conditional move instructions (MOVT64, MOVF64 for scalar;
; X2MOVT32, X2MOVF32 for SIMD X2; X4MOVT16, X4MOVF16 for SIMD X4)
; select per-lane based on SFR flags.
; 3. SFR can also be read/written directly via MOVESFR2GPR/MOVEGPR2SFR.
;
; This test verifies that the instruction selector correctly lowers the
; compare->SFR->conditional-move patterns for both scalar and SIMD cases.
;
; Test strategy: Use -stop-after=instruction-select because standalone
; HaydnInst instructions are currently dropped by the VLIW bundle emitter.

;=============================================================================
; Part 1: SIMD X2 predication (dual 32-bit lanes)
;=============================================================================

;X2SEQ32 -> X2MOVF32: equal compare, move-if-false


define <2 x i32> @test_x2seq_then_movf(<2 x i32> %a, <2 x i32> %b, <2 x i32> %fallthrough) {
  %cmp = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cmp)
  ret <2 x i32> %result
}

;X2SLT32 -> X2MOVT32: less-than compare, move-if-true

; CHECK-LABEL: name: test_x2slt_then_movt
; CHECK: X2SLT32
; CHECK: X2MOVT32
define <2 x i32> @test_x2slt_then_movt(<2 x i32> %a, <2 x i32> %b, <2 x i32> %fallthrough) {
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %fallthrough,<2 x i32> %cmp)
  ret <2 x i32> %result
}

;X2SLE32 -> X2MOVF32: less-or-equal compare, move-if-false

; CHECK-LABEL: name: test_x2sle_then_movf
; CHECK: X2SLE32
; CHECK: X2MOVF32
define <2 x i32> @test_x2sle_then_movf(<2 x i32> %a, <2 x i32> %b, <2 x i32> %fallthrough) {
  %cmp = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cmp)
  ret <2 x i32> %result
}

;=============================================================================
; Part 2: SIMD X4 predication (quad 16-bit lanes)
;=============================================================================

;X4SEQ16 -> X4MOVF16: equal compare, move-if-false

; CHECK-LABEL: name: test_x4seq_then_movf
; CHECK: X4SEQ16
; CHECK: X4MOVF16
define <4 x i16> @test_x4seq_then_movf(<4 x i16> %a, <4 x i16> %b, <4 x i16> %fallthrough) {
  %cmp = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  %result = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %fallthrough,<4 x i16> %cmp)
  ret <4 x i16> %result
}

;X4SLT16 -> X4MOVT16: less-than compare, move-if-true

; CHECK-LABEL: name: test_x4slt_then_movt
; CHECK: X4SLT16
; CHECK: X4MOVT16
define <4 x i16> @test_x4slt_then_movt(<4 x i16> %a, <4 x i16> %b, <4 x i16> %fallthrough) {
  %cmp = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %fallthrough,<4 x i16> %cmp)
  ret <4 x i16> %result
}

;X4SLE16 -> X4MOVT16: less-or-equal compare, move-if-true

; CHECK-LABEL: name: test_x4sle_then_movt
; CHECK: X4SLE16
; CHECK: X4MOVT16
define <4 x i16> @test_x4sle_then_movt(<4 x i16> %a, <4 x i16> %b, <4 x i16> %fallthrough) {
  %cmp = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %fallthrough,<4 x i16> %cmp)
  ret <4 x i16> %result
}

;=============================================================================
; Part 3: Scalar DR64 predication
;=============================================================================

;SEQ64 -> MOVT64: scalar equal compare, move-if-true

; CHECK-LABEL: name: test_scalar_seq_movt
; CHECK: SEQ64
; CHECK: MOVT64
define i64 @test_scalar_seq_movt(i64 %a) {
  %cmp = call i64 @llvm.haydn.seq64(i64 %a, i64 %a)
  %result = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %result
}

;SLT64 -> MOVF64: scalar less-than, move-if-false

; CHECK-LABEL: name: test_scalar_slt_movf
; CHECK: SLT64
; CHECK: MOVF64
define i64 @test_scalar_slt_movf(i64 %a) {
  %cmp = call i64 @llvm.haydn.slt64(i64 %a, i64 %a)
  %result = call i64 @llvm.haydn.movf64(i64 %cmp)
  ret i64 %result
}

;SLE64 -> MOVT64: scalar less-or-equal, move-if-true

; CHECK-LABEL: name: test_scalar_sle_movt
; CHECK: SLE64
; CHECK: MOVT64
define i64 @test_scalar_sle_movt(i64 %a) {
  %cmp = call i64 @llvm.haydn.sle64(i64 %a, i64 %a)
  %result = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %result
}

;=============================================================================
; Part 4: Select-like patterns (CMOV using SFR)
;=============================================================================
; A common DSP pattern is: result = (a < b) ? c : d
; Implemented as: compare(a,b) -> SFR -> movt/movf to select.
; Note: The intrinsics are unary (scalar) or binary (SIMD), and the
; "selection" is implicit in how MOVT/MOVF use the SFR flags set by
; the preceding compare.

;Scalar max pattern using SFR compare+select

; CHECK-LABEL: name: test_scalar_max_pattern
; CHECK: SLT64
; CHECK: MOVT64
define i64 @test_scalar_max_pattern(i64 %a) {
  %cmp = call i64 @llvm.haydn.slt64(i64 %a, i64 %a)
  %sel = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %sel
}

;SIMD conditional select pattern

; CHECK-LABEL: name: test_x2_conditional_select
; CHECK: X2SLT32
; CHECK: X2MOVT32
define <2 x i32> @test_x2_conditional_select(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c) {
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  %sel = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %c,<2 x i32> %cmp)
  ret <2 x i32> %sel
}

;=============================================================================
; Part 5: SFR flag save/restore around predicated operations
;=============================================================================

;Save SFR, do predicated op, restore SFR

; CHECK-LABEL: name: test_sfr_save_around_predication
; CHECK: MOVESFR2GPR
; CHECK: SEQ64
; CHECK: MOVT64
; CHECK: MOVEGPR2SFR
define i64 @test_sfr_save_around_predication(i64 %a, i32 %saved_flags) {
  %saved = call i32 @llvm.haydn.movesfr2gpr()
  %cmp = call i64 @llvm.haydn.seq64(i64 %a, i64 %a)
  %result = call i64 @llvm.haydn.movt64(i64 %cmp)
  call void @llvm.haydn.movegpr2sfr(i32 %saved)
  ret i64 %result
}

;=============================================================================
; Part 6: All compare variants back-to-back
;=============================================================================
; Verify all 3 scalar + 6 SIMD compare intrinsics lower without crash.
; The results are ORed together to prevent the optimizer from eliminating
; dead compares (SIMD compares are marked speculatable and would be
; removed if unused).

; CHECK-LABEL: name: test_all_scalar_compares
; CHECK: SEQ64
; CHECK: SLT64
; CHECK: SLE64
define i64 @test_all_scalar_compares(i64 %a) {
  %c1 = call i64 @llvm.haydn.seq64(i64 %a, i64 %a)
  %c2 = call i64 @llvm.haydn.slt64(i64 %a, i64 %a)
  %c3 = call i64 @llvm.haydn.sle64(i64 %a, i64 %a)
  %r1 = call i64 @llvm.haydn.movt64(i64 %c1)
  %r2 = call i64 @llvm.haydn.movt64(i64 %c2)
  %r3 = call i64 @llvm.haydn.movt64(i64 %c3)
  %or1 = or i64 %r1, %r2
  %or2 = or i64 %or1, %r3
  ret i64 %or2
}

; CHECK-LABEL: name: test_all_x2_compares
; CHECK: X2SEQ32
; CHECK: X2SLT32
; CHECK: X2SLE32
define <2 x i32> @test_all_x2_compares(<2 x i32> %a, <2 x i32> %b) {
  %c1 = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  %c2 = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  %c3 = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  %r1 = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %c1)
  %r2 = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %b,<2 x i32> %c2)
  %r3 = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %r1,<2 x i32> %c3)
  %r2_i = bitcast <2 x i32> %r2 to i64
  %r3_i = bitcast <2 x i32> %r3 to i64
  %or1_i = or i64 %r2_i, %r3_i
  %or1 = bitcast i64 %or1_i to <2 x i32>
  ret <2 x i32> %or1
}

; CHECK-LABEL: name: test_all_x4_compares
; CHECK: X4SEQ16
; CHECK: X4SLT16
; CHECK: X4SLE16
define <4 x i16> @test_all_x4_compares(<4 x i16> %a, <4 x i16> %b) {
  %c1 = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  %c2 = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  %c3 = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  %r1 = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %c1)
  %r2 = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %b,<4 x i16> %c2)
  %r3 = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %r1,<4 x i16> %c3)
  %r2_i = bitcast <4 x i16> %r2 to i64
  %r3_i = bitcast <4 x i16> %r3 to i64
  %or1_i = or i64 %r2_i, %r3_i
  %or1 = bitcast i64 %or1_i to <4 x i16>
  ret <4 x i16> %or1
}

;Intrinsic declarations

; Scalar SFR
declare i64 @llvm.haydn.seq64(i64, i64)
declare i64 @llvm.haydn.slt64(i64, i64)
declare i64 @llvm.haydn.sle64(i64, i64)
declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

; SIMD X2 SFR
declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

; SIMD X4 SFR
declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

; SFR transfer
declare i32 @llvm.haydn.movesfr2gpr()
declare void @llvm.haydn.movegpr2sfr(i32)
