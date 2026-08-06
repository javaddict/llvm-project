; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s

; Role: semantic — SIMD v4i16 and v2i32 shift selection.

; REGRESSION TEST: SIMD v4i16 and v2i32 shift selection
;
; FIXED : v4i16 path used G_EXTRACT_VECTOR_ELT into s32 (element type
; must be s16) → MachineIRBuilder assert. Now extract lane 0 via MOV_DR64_TO_GPR
; as GPR32 shift amount for X4SLL16/X4SRA16/X4SRL16 and X2SLL32/X2SRA32/X2SRL32.
;
; Expect mnemonics: x4sll16/x4sra16/x4srl16 and x2sll32/x2sra32/x2srl32.

; v4i16 left shift

define <4 x i16> @test_v4i16_shl(<4 x i16> %a, <4 x i16> %b) {
; CHECK: x4sll16
  %r = shl <4 x i16> %a, %b
  ret <4 x i16> %r
}

; v4i16 arithmetic right shift
define <4 x i16> @test_v4i16_ashr(<4 x i16> %a, <4 x i16> %b) {
; CHECK: x4sra16
  %r = ashr <4 x i16> %a, %b
  ret <4 x i16> %r
}

; v4i16 logical right shift
define <4 x i16> @test_v4i16_lshr(<4 x i16> %a, <4 x i16> %b) {
; CHECK: x4srl16
  %r = lshr <4 x i16> %a, %b
  ret <4 x i16> %r
}

; v2i32 left shift
define <2 x i32> @test_v2i32_shl(<2 x i32> %a, <2 x i32> %b) {
; CHECK: x2sll32
  %r = shl <2 x i32> %a, %b
  ret <2 x i32> %r
}

; v2i32 arithmetic right shift
define <2 x i32> @test_v2i32_ashr(<2 x i32> %a, <2 x i32> %b) {
; CHECK: x2sra32
  %r = ashr <2 x i32> %a, %b
  ret <2 x i32> %r
}

; v2i32 logical right shift
define <2 x i32> @test_v2i32_lshr(<2 x i32> %a, <2 x i32> %b) {
; CHECK: x2srl32
  %r = lshr <2 x i32> %a, %b
  ret <2 x i32> %r
}
