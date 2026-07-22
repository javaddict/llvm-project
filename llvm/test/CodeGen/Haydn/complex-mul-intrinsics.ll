; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: Wave 3 complex multiply intrinsics for FFT butterfly operations.
;
; (Path B): X2CMUL32/X2CMUL32S are TRUE 2-output (golden
; DR_Write_Port=[rtd1,rtd2]). The intrinsic now returns {i64, i64} (real ->
; rtd1, imag -> rtd2). This also fixes the B2 "Explicit definition marked
; as use" verifier abort (the D_RR2 2-dest dag binds all fields, no orphan).
;
; Tests that all 6 complex multiply intrinsics lower to the correct Haydn
; instructions. The X4FCMUL* variants are binary DR64 (FmtALU64); the
; X2CMUL* variants are 2-dest DR64 (D_RR2 _FLEX, was FmtMAC legacy).
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;Quad 16-bit complex multiply (binary DR64)

; CHECK-LABEL: test_x4fcmul16rs:
; CHECK: x4fcmul16rs
define i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <4 x i16>
  %bc.2 = bitcast i64 %b to <4 x i16>
  %call.3 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.1, <4 x i16> %bc.2)
  %r = bitcast <4 x i16> %call.3 to i64
  ret i64 %r
}

; CHECK-LABEL: test_x4fcmula16rs:
; CHECK: x4fcmula16rs
; X4fcmula16rs is a ternary accumulator (acc, a, b) — reads rtd per DB.
define i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
  %bc.4 = bitcast i64 %acc to <4 x i16>
  %bc.5 = bitcast i64 %a to <4 x i16>
  %bc.6 = bitcast i64 %b to <4 x i16>
  %call.7 = call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %bc.4, <4 x i16> %bc.5, <4 x i16> %bc.6)
  %r = bitcast <4 x i16> %call.7 to i64
  ret i64 %r
}

; CHECK-LABEL: test_x4fcmul16rss:
; CHECK: x4fcmul16rss
define i64 @test_x4fcmul16rss(i64 %a, i64 %b) {
  %bc.8 = bitcast i64 %a to <4 x i16>
  %bc.9 = bitcast i64 %b to <4 x i16>
  %call.10 = call <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16> %bc.8, <4 x i16> %bc.9)
  %r = bitcast <4 x i16> %call.10 to i64
  ret i64 %r
}

; CHECK-LABEL: test_x4fcmula16rss:
; CHECK: x4fcmula16rss
; Ternary accumulator form.
define i64 @test_x4fcmula16rss(i64 %acc, i64 %a, i64 %b) {
  %bc.11 = bitcast i64 %acc to <4 x i16>
  %bc.12 = bitcast i64 %a to <4 x i16>
  %bc.13 = bitcast i64 %b to <4 x i16>
  %call.14 = call <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16> %bc.11, <4 x i16> %bc.12, <4 x i16> %bc.13)
  %r = bitcast <4 x i16> %call.14 to i64
  ret i64 %r
}

;Dual 32-bit complex multiply (Path B: 2-dest DR64)

; CHECK-LABEL: test_x2cmul32:
; CHECK: x2cmul32
define i64 @test_x2cmul32(i64 %a, i64 %b) {
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.15, <2 x i32> %bc.16)
  %real = extractvalue { i64, i64 } %r, 0
  ret i64 %real
}

; CHECK-LABEL: test_x2cmul32s:
; CHECK: x2cmul32s
define i64 @test_x2cmul32s(i64 %a, i64 %b) {
  %bc.17 = bitcast i64 %a to <2 x i32>
  %bc.18 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32> %bc.17, <2 x i32> %bc.18)
  %real = extractvalue { i64, i64 } %r, 0
  ret i64 %real
}

;Intrinsics declarations

declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16>, <4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16>, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32>, <2 x i32>)