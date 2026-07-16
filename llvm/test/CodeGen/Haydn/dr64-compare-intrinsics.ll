; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; Status : R_CMP operand-flag XFAIL stale; CHECKs use DAG for bundle order.
; REGRESSION TEST: DR64 comparison intrinsics — SFR/compare/conditional-move.
;
; Bug (fixed,): SFR/compare/conditional-move instructions
; (SLT64, SLE64, SEQ64, MOVT64, MOVF64, MOVESFR2GPR, MOVEGPR2SFR, ZERO_SFR
; X2SEQ32/SLT32/SLE32/MOVF32/MOVT32, X4SEQ16/SLT16/SLE16/MOVF16/MOVT16)
; were defined in HaydnInstrInfoAuto.td as HaydnInst<4,...> blanket pseudos
; with no Inst{...}=... fields. TableGen marked them MCID::Pseudo and the
; AsmPrinter silently dropped them — the instructions survived ISel but
; never reached assembly.
;
; Fix: real 32-bit R-type encodings assigned in HaydnInstrInfo.td under
; opcode 0x67, funct 0x187..0x196. Each mnemonic now reaches
; assembly. If a regression reintroduces the blanket pseudo (no Inst{}
; fields), the corresponding CHECK line fails because the mnemonic
; disappears from the output.

;===----------------------------------------------------------------------===;
; Scalar 64-bit SFR compare (unary DR64, IntrHasSideEffects)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.slt64(i64)
declare i64 @llvm.haydn.sle64(i64)
declare i64 @llvm.haydn.seq64(i64)

define dso_local i64 @test_slt64(i64 %a) {
; CHECK-LABEL: test_slt64:
; CHECK: slt64
  %r = call i64 @llvm.haydn.slt64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_sle64(i64 %a) {
; CHECK-LABEL: test_sle64:
; CHECK: sle64
  %r = call i64 @llvm.haydn.sle64(i64 %a)
  ret i64 %r
}

define dso_local i64 @test_seq64(i64 %a) {
; CHECK-LABEL: test_seq64:
; CHECK: seq64
  %r = call i64 @llvm.haydn.seq64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Scalar 64-bit SFR conditional move (unary DR64, IntrHasSideEffects)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.movt64(i64)
declare i64 @llvm.haydn.movf64(i64)

define dso_local i64 @test_movt64(i64 %a) {
; CHECK-LABEL: test_movt64:
; CHECK: slt64
; CHECK: movt64
  %cmp = call i64 @llvm.haydn.slt64(i64 %a)
  %r = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %r
}

define dso_local i64 @test_movf64(i64 %a) {
; CHECK-LABEL: test_movf64:
; CHECK: sle64
; CHECK: movf64
  %cmp = call i64 @llvm.haydn.sle64(i64 %a)
  %r = call i64 @llvm.haydn.movf64(i64 %cmp)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; SFR register transfer
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.movesfr2gpr()
declare void @llvm.haydn.movegpr2sfr(i32)
declare void @llvm.haydn.zero.sfr()

define dso_local i32 @test_movesfr2gpr() {
; CHECK-LABEL: test_movesfr2gpr:
; CHECK: movesfr2gpr
  %r = call i32 @llvm.haydn.movesfr2gpr()
  ret i32 %r
}

define dso_local void @test_movegpr2sfr(i32 %val) {
; CHECK-LABEL: test_movegpr2sfr:
; CHECK: movegpr2sfr
  call void @llvm.haydn.movegpr2sfr(i32 %val)
  ret void
}

define dso_local void @test_zero_sfr() {
; CHECK-LABEL: test_zero_sfr:
; CHECK: zero_sfr
  call void @llvm.haydn.zero.sfr()
  ret void
}

;===----------------------------------------------------------------------===;
; Scalar predication pattern: compare -> SFR -> conditional select
;===----------------------------------------------------------------------===;

define dso_local i64 @test_scalar_predication(i64 %a) {
; CHECK-LABEL: test_scalar_predication:
; CHECK: slt64
; CHECK: movt64
  %cmp = call i64 @llvm.haydn.slt64(i64 %a)
  %result = call i64 @llvm.haydn.movt64(i64 %cmp)
  ret i64 %result
}

;===----------------------------------------------------------------------===;
; SFR save/restore via GPR
;===----------------------------------------------------------------------===;

define dso_local i64 @test_sfr_save_restore(i64 %a, i32 %saved_sfr) {
; CHECK-LABEL: test_sfr_save_restore:
; CHECK-DAG: movegpr2sfr
; CHECK-DAG: slt64
  call void @llvm.haydn.movegpr2sfr(i32 %saved_sfr)
  %r = call i64 @llvm.haydn.slt64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 SIMD compare -> SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2seq32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2seq32:
; CHECK: x2seq32
  %r = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2slt32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2slt32:
; CHECK: x2slt32
  %r = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2sle32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sle32:
; CHECK: x2sle32
  %r = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X2 SIMD conditional move based on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2movf32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
; CHECK-LABEL: test_x2movf32:
; CHECK: x2movf32
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2movt32(<2 x i32> %fallthrough, <2 x i32> %cond_val) {
; CHECK-LABEL: test_x2movt32:
; CHECK: x2movt32
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %fallthrough,<2 x i32> %cond_val)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD compare -> SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4seq16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seq16:
; CHECK: x4seq16
  %r = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4slt16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4slt16:
; CHECK: x4slt16
  %r = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4sle16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4sle16:
; CHECK: x4sle16
  %r = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD conditional move based on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4movf16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
; CHECK-LABEL: test_x4movf16:
; CHECK: x4movf16
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4movt16(<4 x i16> %fallthrough, <4 x i16> %cond_val) {
; CHECK-LABEL: test_x4movt16:
; CHECK: x4movt16
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %fallthrough,<4 x i16> %cond_val)
  ret <4 x i16> %r
}
