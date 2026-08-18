; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Role: semantic — X2SRL32 takes one GPR32 amount (lane 0). Generic
; `lshr <2 x i32>` is per-lane. SLP in nettle-sha256 σ0/σ1 emits
; `<i32 10, i32 3>`; treating that as x2srl32-by-10 miscompiles the digest.
; Non-splat constant amounts must become two scalar shifts.

define i32 @nonsplat_lshr_v2i32(i32 %a, i32 %b) {
; CHECK-LABEL: nonsplat_lshr_v2i32:
; CHECK-NOT: x2srl32
; CHECK:     srl
; CHECK:     srl
  %v0 = insertelement <2 x i32> poison, i32 %a, i32 0
  %v = insertelement <2 x i32> %v0, i32 %b, i32 1
  %s = lshr <2 x i32> %v, <i32 10, i32 3>
  %e0 = extractelement <2 x i32> %s, i32 0
  %e1 = extractelement <2 x i32> %s, i32 1
  %x = xor i32 %e0, %e1
  ret i32 %x
}

define <2 x i32> @splat_lshr_v2i32(<2 x i32> %a) {
; CHECK-LABEL: splat_lshr_v2i32:
; CHECK: x2srl32
  %s = lshr <2 x i32> %a, <i32 10, i32 10>
  ret <2 x i32> %s
}
