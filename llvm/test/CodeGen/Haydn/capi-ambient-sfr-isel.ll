; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -enable-post-misched=false -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 \
; RUN:     -enable-post-misched=false -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — ambient SFR quarantine ISel exit: x2/x4 movt/movf are IntrHasSideEffects → G_INTRINSIC_W_SIDE_EFFECTS.
; Post-RA pack of two-epoch reverse is format-pipeline owned; this pin is ISel emit.

; C1.2 ambient SFR quarantine ISel exit:
;   x2/x4 movt/movf are IntrHasSideEffects → G_INTRINSIC_W_SIDE_EFFECTS
;   (already allowlisted) and select to X2MOVT32 / X2MOVF32 / X4MOVT16 /
;   X4MOVF16. Two-epoch ambient reverse-order keeps two SLT + two MOVT.
;   AE path uses pure SSA cmplt/mux (see capi-pred-ssa-isel.ll).


define <2 x i32> @test_x2movt32(<2 x i32> %ft, <2 x i32> %cv) {
  %r = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %ft, <2 x i32> %cv)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x2movf32:
; CHECK: x2movf32
define <2 x i32> @test_x2movf32(<2 x i32> %ft, <2 x i32> %cv) {
  %r = call <2 x i32> @llvm.haydn.x2movf32(<2 x i32> %ft, <2 x i32> %cv)
  ret <2 x i32> %r
}

; CHECK-LABEL: test_x4movt16:
; CHECK: x4movt16
define <4 x i16> @test_x4movt16(<4 x i16> %ft, <4 x i16> %cv) {
  %r = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %ft, <4 x i16> %cv)
  ret <4 x i16> %r
}

; CHECK-LABEL: test_x4movf16:
; CHECK: x4movf16
define <4 x i16> @test_x4movf16(<4 x i16> %ft, <4 x i16> %cv) {
  %r = call <4 x i16> @llvm.haydn.x4movf16(<4 x i16> %ft, <4 x i16> %cv)
  ret <4 x i16> %r
}

; Ambient two epochs reverse consumption: both SLT writes and both MOVT
; reads must select (attribute flip must not invent new selectors).
; CHECK-LABEL: ambient_two_epoch:
; CHECK-DAG: x2slt32
; CHECK-DAG: x2movt32
; CHECK-DAG: x2slt32
; CHECK-DAG: x2movt32
define <2 x i32> @ambient_two_epoch(<2 x i32> %a1, <2 x i32> %b1,
                                    <2 x i32> %a2, <2 x i32> %b2,
                                    <2 x i32> %t1, <2 x i32> %f1,
                                    <2 x i32> %t2, <2 x i32> %f2) {
  %c1 = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a1, <2 x i32> %b1)
  %r1 = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %f1, <2 x i32> %t1)
  %c2 = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a2, <2 x i32> %b2)
  %r2 = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %f2, <2 x i32> %t2)
  %sum = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %r1, <2 x i32> %r2)
  ; Keep compare results live so DCE cannot drop them for other reasons.
  %keep = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %c1, <2 x i32> %c2)
  %out = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %sum, <2 x i32> %keep)
  ret <2 x i32> %out
}

declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
