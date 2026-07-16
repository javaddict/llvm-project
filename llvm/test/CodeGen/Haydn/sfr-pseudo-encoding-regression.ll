; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REGRESSION FILED : DR64 R_CMP instructions (SLT64_S1
; SEQ64_S1, SLE64_S1 and _S0/_S2 variants) have an operand-flag
; bug in HaydnFormatsALU64.td that aborts the MachineVerifier ("Explicit
; operand marked as def"). The.td marks operand 0 ($rd) as a def with
; hasSideEffects = 1 (implicit SFR write) — the verifier rejects this
; combination. Regressed in the Flex cutover. Selector is correct
; (MIR shows real emission); only the.td operand flags need adjusting.
; Track under / Flex cutover (DR64 R_CMP operand flags). Do NOT
; rebaseline CHECKs to silence the verifier abort.
;
; REGRESSION TEST: SFR/compare/conditional-move/sel-imm pseudo-drops.
;
; Bug: SLT64, SLE64, SEQ64, MOVT64, MOVF64, MOVESFR2GPR, MOVEGPR2SFR
; ZERO_SFR, X2SEQ32, X2SLT32, X2SLE32, X2MOVF32, X2MOVT32, X4SEQ16
; X4SLT16, X4SLE16, X4MOVF16, X4MOVT16, X2ABS32S, X4SELI16 were defined
; in HaydnInstrInfoAuto.td as HaydnInst<4,...> blanket pseudos with no
; Inst{...}=... fields. TableGen marked them MCID::Pseudo and AsmPrinter
; silently dropped them — the instructions survived ISel but never reached
; assembly. Function bodies emitted as { xor32 r0, r0, r0; nop; nop }.
;
; Fix : real 32-bit R-type encodings assigned in HaydnInstrInfo.td
; under opcode 0x67, funct 0x187..0x19A. Each mnemonic now reaches
; assembly. If a regression reintroduces the blanket pseudo (no Inst{}
; fields), the corresponding CHECK line fails because the mnemonic
; disappears from the output.
;
; Test design: each intrinsic is invoked in its own function so the
; mnemonic must appear in the assembly. The CHECK lines only assert
; presence of the mnemonic (not full bundle formatting), which is the
; minimal contract — emitting the right MCInst is the bug being guarded.

;===----------------------------------------------------------------------===;
; Scalar SFR compare (unary DR64 → sets SFR)
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
; Scalar SFR conditional move (unary DR64 → reads SFR)
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

define dso_local void @test_movegpr2sfr(i32 %v) {
; CHECK-LABEL: test_movegpr2sfr:
; CHECK: movegpr2sfr
  call void @llvm.haydn.movegpr2sfr(i32 %v)
  ret void
}

define dso_local void @test_zero_sfr() {
; CHECK-LABEL: test_zero_sfr:
; CHECK: zero_sfr
  call void @llvm.haydn.zero.sfr()
  ret void
}

;===----------------------------------------------------------------------===;
; X2 SIMD SFR compare (binary DR64 → sets SFR)
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
; X2 SIMD conditional move on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2movf32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2movf32:
; CHECK: x2movf32
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2movt32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2movt32:
; CHECK: x2movt32
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD SFR compare (binary DR64 → sets SFR)
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
; X4 SIMD conditional move on SFR (binary DR64)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

define dso_local <4 x i16> @test_x4movf16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4movf16:
; CHECK: x4movf16
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4movt16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4movt16:
; CHECK: x4movt16
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2ABS32S — unary DR64 (per-lane saturating absolute value)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2abs32s(<2 x i32>)

define dso_local <2 x i32> @test_x2abs32s(<2 x i32> %a) {
; CHECK-LABEL: test_x2abs32s:
; CHECK: x2abs32s
  %r = call <2 x i32> @llvm.haydn.x2abs32s(<2 x i32> %a)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4SELI16 — select with 4-bit immediate lane mask.
; Per spec, the third operand is uimm4 (constant 0..15). The selector
; correctly rejects non-constant operands; this test exercises the
; constant-imm path.
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

define dso_local <4 x i16> @test_x4seli16_const5(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16_const5:
; CHECK: x4seli16
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 5)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_const0(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16_const0:
; CHECK: x4seli16
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 0)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4seli16_const15(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16_const15:
; CHECK: x4seli16
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 15)
  ret <4 x i16> %r
}
