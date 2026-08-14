; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — 64-bit SIMD vectors pass/return in DR (D0…), not multi-GPR/sret.

; G-ABI-VEC: 64-bit SIMD vectors pass/return in DR (D0…), not multi-GPR/sret.
; Identity return may be empty (arg already D0); arithmetic uses X2/X4 on D*.


define <2 x i32> @ret_v2i32(<2 x i32> %v) {
  ret <2 x i32> %v
}

; CHECK-LABEL: ret_v4i16:
; CHECK: jalr
define <4 x i16> @ret_v4i16(<4 x i16> %v) {
  ret <4 x i16> %v
}

; CHECK-LABEL: add_v2i32:
; CHECK: x2add32
; CHECK-NOT: add32 {{r[0-9]+}}
define <2 x i32> @add_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %r = add <2 x i32> %a, %b
  ret <2 x i32> %r
}

; CHECK-LABEL: take_v2i32:
; CHECK: x2add32 {{d[0-9]+}}
define <2 x i32> @take_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %s = add <2 x i32> %a, %b
  ret <2 x i32> %s
}
