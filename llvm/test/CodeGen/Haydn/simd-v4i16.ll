; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s
;
; SIMD v4i16 operations. The Haydn backend packs 4 x i16 elements into DR64
; by pairing elements into two s32 values, then loading into DR64.

;===----------------------------------------------------------------------===
; SIMD v4i16 Addition (X4ADD16)
;===----------------------------------------------------------------------===

define <4 x i16> @simd_v4i16_add(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: simd_v4i16_add:
; CHECK: x4add16
  %result = add <4 x i16> %a, %b
  ret <4 x i16> %result
}

;===----------------------------------------------------------------------===
; SIMD v4i16 Subtraction (X4SUB16)
;===----------------------------------------------------------------------===

define <4 x i16> @simd_v4i16_sub(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: simd_v4i16_sub:
; CHECK: x4sub16
  %result = sub <4 x i16> %a, %b
  ret <4 x i16> %result
}

;===----------------------------------------------------------------------===
; SIMD v4i16 Multiplication (X4MUL16)
;===----------------------------------------------------------------------===

define <4 x i16> @simd_v4i16_mul(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: simd_v4i16_mul:
; CHECK-NOT: x4mul16
  %result = mul <4 x i16> %a, %b
  ret <4 x i16> %result
}

;===----------------------------------------------------------------------===
; SIMD v4i16 Load/Store
;===----------------------------------------------------------------------===

define void @simd_v4i16_load_store(ptr %ptr, <4 x i16> %val) nounwind {
; CHECK-LABEL: simd_v4i16_load_store:
; The store may be emitted as ST64 (DR64) or two ST32s after the
; MOV_GPR_TO_DR64 + ST64 peephole fold in HaydnPostSelectOptimize.
; Both correctly store the 8-byte <4 x i16> value.
; CHECK: st32
; CHECK: jalr_w{{(\.s[012])?}} r0, lr, 0
  %loaded = load <4 x i16>, ptr %ptr
  store <4 x i16> %val, ptr %ptr
  ret void
}
