; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck --check-prefix=MIR %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   < %s | FileCheck --check-prefix=ASM %s

; Role: MIR — ISel must emit logical MOVEI_H/L and X4CMUL16* only (no pre-RA _S* members).

; ISel must emit logical MOVEI_H/L and X4CMUL16* only (no pre-RA _S* members).
; Final asm mnemonics stay movei_h / x4cmul16 after post-RA setDesc.

declare i64 @llvm.haydn.movei.h(i32)
declare i64 @llvm.haydn.movei.l(i32)
declare <2 x i32> @llvm.haydn.x4cmul16(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16.f2(<4 x i16>)
declare <2 x i32> @llvm.haydn.x4cmul16s.f2(<4 x i16>)

define i64 @test_movei_h() {
; MIR-LABEL: name: test_movei_h
; MIR: MOVEI_H
; ASM-LABEL: test_movei_h:
; ASM: movei_h
  %r = call i64 @llvm.haydn.movei.h(i32 4660)
  ret i64 %r
}

define i64 @test_movei_l() {
; MIR-LABEL: name: test_movei_l
; MIR: MOVEI_L
; ASM-LABEL: test_movei_l:
; ASM: movei_l
  %r = call i64 @llvm.haydn.movei.l(i32 22136)
  ret i64 %r
}

define <2 x i32> @test_x4cmul16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16
; MIR: X4CMUL16
; ASM-LABEL: test_x4cmul16:
; ASM: {{x4cmul16[^s]}}
  %r = call <2 x i32> @llvm.haydn.x4cmul16(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16s
; MIR: X4CMUL16S{{[^_]}}
; ASM-LABEL: test_x4cmul16s:
; ASM: x4cmul16s
  %r = call <2 x i32> @llvm.haydn.x4cmul16s(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16_f2(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16_f2
; MIR: X4CMUL16_F2
; ASM-LABEL: test_x4cmul16_f2:
; ASM: x4cmul16.f2
  %r = call <2 x i32> @llvm.haydn.x4cmul16.f2(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16s_f2(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16s_f2
; MIR: X4CMUL16S_F2
; ASM-LABEL: test_x4cmul16s_f2:
; ASM: x4cmul16s.f2
  %r = call <2 x i32> @llvm.haydn.x4cmul16s.f2(<4 x i16> %a)
  ret <2 x i32> %r
}

; REGRESSION TEST: ISel must not emit *_S1/*_S2 for cmpsel / ZERO_DR.
; Bug: haydn_x2cmpsel32 selected X2SLT32+X2MOVT32 (and x4 analog);
; haydn_zero_dr selected ZERO_DR because the Auto logical had a fake
; simm16. D493: pre-RA logical only. If this regresses, -stop-after=instruction-select
; MIR contains _S1/_S2 opcodes.

declare <2 x i32> @llvm.haydn.x2cmpsel32(<2 x i32>, <2 x i32>, <2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4cmpsel16(<4 x i16>, <4 x i16>, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.zero.dr()

define <2 x i32> @test_x2cmpsel32(<2 x i32> %a, <2 x i32> %b,
                                 <2 x i32> %t, <2 x i32> %f) {
; MIR-LABEL: name: test_x2cmpsel32
; MIR: X2SLT32
; MIR-NOT: X2SLT32
; MIR-NOT: X2SLT32
; MIR: X2MOVT32
; MIR-NOT: X2MOVT32
; MIR-NOT: X2MOVT32
; ASM-LABEL: test_x2cmpsel32:
; ASM: x2slt32
; ASM: x2movt32
  %r = call <2 x i32> @llvm.haydn.x2cmpsel32(
      <2 x i32> %a, <2 x i32> %b, <2 x i32> %t, <2 x i32> %f)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4cmpsel16(<4 x i16> %a, <4 x i16> %b,
                                 <4 x i16> %t, <4 x i16> %f) {
; MIR-LABEL: name: test_x4cmpsel16
; MIR: X4SLT16
; MIR-NOT: X4SLT16
; MIR-NOT: X4SLT16
; MIR: X4MOVT16
; MIR-NOT: X4MOVT16
; MIR-NOT: X4MOVT16
; ASM-LABEL: test_x4cmpsel16:
; ASM: x4slt16
; ASM: x4movt16
  %r = call <4 x i16> @llvm.haydn.x4cmpsel16(
      <4 x i16> %a, <4 x i16> %b, <4 x i16> %t, <4 x i16> %f)
  ret <4 x i16> %r
}

define i64 @test_zero_dr() {
; MIR-LABEL: name: test_zero_dr
; MIR: ZERO_DR
; MIR-NOT: ZERO_DR
; MIR-NOT: ZERO_DR
; ASM-LABEL: test_zero_dr:
; ASM: zero_dr
  %r = call i64 @llvm.haydn.zero.dr()
  ret i64 %r
}
