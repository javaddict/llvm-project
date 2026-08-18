; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s

; Role: semantic — residual non-native vectors (v2i16) scalarize after
; G-ABI-VEC registers only v2i32/v4i16/v8i8/v2f32 for DR ABI. CoreMark SLP
; can produce <2 x i16> G_ADD. 32-bit residual packs pass in one GPR
; (CC_Haydn v2i16/v4i8). ISel pin (ADD32 + RET); asm print is a later pass.

define void @add_v2i16_mem(ptr %p, ptr %q, ptr %r) nounwind {
; CHECK-LABEL: name: add_v2i16_mem
; CHECK: ADD32
; CHECK: RET
  %a = load <2 x i16>, ptr %p
  %b = load <2 x i16>, ptr %q
  %s = add <2 x i16> %a, %b
  store <2 x i16> %s, ptr %r
  ret void
}

; CHECK-LABEL: name: zext_v2i16_arg
; CHECK: RET
define <2 x i32> @zext_v2i16_arg(<2 x i16> %x) nounwind {
  %e0 = extractelement <2 x i16> %x, i32 0
  %e1 = extractelement <2 x i16> %x, i32 1
  %z0 = zext i16 %e0 to i32
  %z1 = zext i16 %e1 to i32
  %r0 = insertelement <2 x i32> poison, i32 %z0, i32 0
  %r1 = insertelement <2 x i32> %r0, i32 %z1, i32 1
  ret <2 x i32> %r1
}

; CHECK-LABEL: name: take_v8i8
; CHECK: ADD32
; CHECK: RET
define <8 x i8> @take_v8i8(<8 x i8> %a, <8 x i8> %b) nounwind {
  %r = add <8 x i8> %a, %b
  ret <8 x i8> %r
}
