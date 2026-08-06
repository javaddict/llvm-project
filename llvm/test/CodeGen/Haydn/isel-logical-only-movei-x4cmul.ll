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
; MIR-NOT: MOVEI_H_S0
; ASM-LABEL: test_movei_h:
; ASM: movei_h
  %r = call i64 @llvm.haydn.movei.h(i32 4660)
  ret i64 %r
}

define i64 @test_movei_l() {
; MIR-LABEL: name: test_movei_l
; MIR: MOVEI_L
; MIR-NOT: MOVEI_L_S0
; ASM-LABEL: test_movei_l:
; ASM: movei_l
  %r = call i64 @llvm.haydn.movei.l(i32 22136)
  ret i64 %r
}

define <2 x i32> @test_x4cmul16(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16
; MIR: X4CMUL16
; MIR-NOT: X4CMUL16_S1
; MIR-NOT: X4CMUL16_S2
; ASM-LABEL: test_x4cmul16:
; ASM: {{x4cmul16[^s]}}
  %r = call <2 x i32> @llvm.haydn.x4cmul16(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16s(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16s
; MIR: X4CMUL16S{{[^_]}}
; MIR-NOT: X4CMUL16S_S1
; MIR-NOT: X4CMUL16S_S2
; ASM-LABEL: test_x4cmul16s:
; ASM: x4cmul16s
  %r = call <2 x i32> @llvm.haydn.x4cmul16s(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16_f2(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16_f2
; MIR: X4CMUL16_F2
; MIR-NOT: X4CMUL16_F2_S1
; MIR-NOT: X4CMUL16_F2_S2
; ASM-LABEL: test_x4cmul16_f2:
; ASM: x4cmul16.f2
  %r = call <2 x i32> @llvm.haydn.x4cmul16.f2(<4 x i16> %a)
  ret <2 x i32> %r
}

define <2 x i32> @test_x4cmul16s_f2(<4 x i16> %a) {
; MIR-LABEL: name: test_x4cmul16s_f2
; MIR: X4CMUL16S_F2
; MIR-NOT: X4CMUL16S_F2_S1
; MIR-NOT: X4CMUL16S_F2_S2
; ASM-LABEL: test_x4cmul16s_f2:
; ASM: x4cmul16s.f2
  %r = call <2 x i32> @llvm.haydn.x4cmul16s.f2(<4 x i16> %a)
  ret <2 x i32> %r
}
