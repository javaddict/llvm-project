; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Golden-lane selection: plain lanewise IR add/sub on DR64 vector types
; must map to X2ADD32 / X4ADD16 (and the matching sub mnemonics), not
; scalar i64 add sequences or bag bitcast + i64 arithmetic.
;
; Companion Clang test: clang/test/CodeGen/Haydn/vector-add-asm.c
; (C `a + b` on haydn_x2int32 / haydn_x4int16 → same mnemonics).

; CHECK-LABEL: add_v2i32:
; CHECK: x2add32
; CHECK-NOT: add64
define <2 x i32> @add_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %r = add <2 x i32> %a, %b
  ret <2 x i32> %r
}

; CHECK-LABEL: add_v4i16:
; CHECK: x4add16
; CHECK-NOT: add64
define <4 x i16> @add_v4i16(<4 x i16> %a, <4 x i16> %b) {
  %r = add <4 x i16> %a, %b
  ret <4 x i16> %r
}

; CHECK-LABEL: sub_v2i32:
; CHECK: x2sub32
define <2 x i32> @sub_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %r = sub <2 x i32> %a, %b
  ret <2 x i32> %r
}

; CHECK-LABEL: sub_v4i16:
; CHECK: x4sub16
define <4 x i16> @sub_v4i16(<4 x i16> %a, <4 x i16> %b) {
  %r = sub <4 x i16> %a, %b
  ret <4 x i16> %r
}

; Also cover the saturating intrinsic path used by haydn_x2add32s / x4add16s
; (Public ExtVector surface → ClangBuiltin → these llvm.haydn.* calls).

declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)

; CHECK-LABEL: add_v2i32_intrin:
; CHECK: x2add32
define <2 x i32> @add_v2i32_intrin(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: add_v4i16_intrin:
; CHECK: x4add16
define <4 x i16> @add_v4i16_intrin(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4add16(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}

; CHECK-LABEL: add_v2i32s_intrin:
; CHECK: x2add32s
define <2 x i32> @add_v2i32s_intrin(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

; CHECK-LABEL: add_v4i16s_intrin:
; CHECK: x4add16s
define <4 x i16> @add_v4i16s_intrin(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a, <4 x i16> %b)
  ret <4 x i16> %r
}
