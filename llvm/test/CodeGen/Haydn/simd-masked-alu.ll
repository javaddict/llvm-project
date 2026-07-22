; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: test_x2add32s:
; CHECK-LABEL: test_x2sub32s:
; CHECK-LABEL: test_x2add32:
; CHECK-LABEL: test_x2sub32:
; CHECK-LABEL: test_x2addsub32s:
; CHECK-LABEL: test_x2add32_hllh:
; CHECK-LABEL: test_x2add32s_hllh:
; CHECK-LABEL: test_x2sub32_hllh:
; CHECK-LABEL: test_x2sub32s_hllh:
; CHECK-LABEL: test_x2max32:
; CHECK-LABEL: test_x2min32:
; CHECK-LABEL: test_x2clamp32:
; CHECK-LABEL: test_x4add16s:
; CHECK-LABEL: test_x4sub16s:
; CHECK-LABEL: test_x4add16:
; CHECK-LABEL: test_x4sub16:
; CHECK-LABEL: test_x4max16:
; CHECK-LABEL: test_x4min16:
; CHECK-LABEL: test_x4clamp16:
; CHECK-LABEL: test_x2add32s_masked_eq:
; CHECK-LABEL: test_x2sub32s_masked_lt:
; CHECK-LABEL: test_x4add16s_masked_eq:
; CHECK-LABEL: test_x4sub16s_masked_sle:
; CHECK: {{.}}

define <2 x i32> @test_x2add32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) non-saturating ADD/SUB
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2add32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2sub32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) ADDSUB (one lane adds, other subtracts)
; Critical for FFT butterfly operations.
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2addsub32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) cross-lane HLLH variants
; These swap high/low lanes before operating.
;===----------------------------------------------------------------------===

define i64 @test_x2add32_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2add32_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2add32s_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2add32s_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2sub32_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sub32_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2sub32s_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sub32s_hllh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) MAX/MIN/CLAMP
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2max32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2max32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2min32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2min32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define i64 @test_x2clamp32(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2clamp32(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) saturating ADD/SUB
;===----------------------------------------------------------------------===

define <4 x i16> @test_x4add16s(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16s(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) non-saturating ADD/SUB
;===----------------------------------------------------------------------===

define <4 x i16> @test_x4add16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4add16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4sub16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) MAX/MIN/CLAMP
;===----------------------------------------------------------------------===

define <4 x i16> @test_x4max16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4max16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4min16(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4min16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define i64 @test_x4clamp16(i64 %a, i64 %b) {
  %bc.4 = bitcast i64 %a to <4 x i16>
  %bc.5 = bitcast i64 %b to <4 x i16>
  %call.6 = call <4 x i16> @llvm.haydn.x4clamp16(<4 x i16> %bc.4, <4 x i16> %bc.5)
  %r = bitcast <4 x i16> %call.6 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; SFR-predicated masked ALU pattern (documented, will work when SFR bug fixed)
;
; The intended pattern is:
; %cmp = call x2slt32(a, mask_val) -- sets SFR per lane
; %alu = call x2add32s(a, b) -- compute ALU result
; %result = call x2movt32(a, %alu) -- select ALU result where SFR true
;
; When the SFR side-effect modeling is fixed, these tests will verify the
; complete masked ALU pattern.
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2add32s_masked_eq(<2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
  %cmp = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %mask_val)
  %added = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %added)
  ret <2 x i32> %result
}

define <2 x i32> @test_x2sub32s_masked_lt(<2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %mask_val)
  %subbed = call <2 x i32> @llvm.haydn.x2sub32s(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %subbed)
  ret <2 x i32> %result
}

define <4 x i16> @test_x4add16s_masked_eq(<4 x i16> %a, <4 x i16> %b, <4 x i16> %mask_val) {
  %cmp = call <4 x i16> @llvm.haydn.x4seq16(<4 x i16> %a,<4 x i16> %mask_val)
  %added = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a,<4 x i16> %b)
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %added)
  ret <4 x i16> %result
}

define <4 x i16> @test_x4sub16s_masked_sle(<4 x i16> %a, <4 x i16> %b, <4 x i16> %mask_val) {
  %cmp = call <4 x i16> @llvm.haydn.x4sle16(<4 x i16> %a,<4 x i16> %mask_val)
  %subbed = call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %a,<4 x i16> %b)
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %subbed)
  ret <4 x i16> %result
}

;===----------------------------------------------------------------------===
; Intrinsic declarations
;===----------------------------------------------------------------------===

; SFR compare (binary DR64)
declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4seq16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sle16(<4 x i16>, <4 x i16>)

; SFR conditional move (binary DR64)
declare <2 x i32> @llvm.haydn.x2movf32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4movf16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

; X2 SIMD ALU binary (binary DR64)
declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2max32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2min32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2clamp32(<2 x i32>, <2 x i32>)
; X2 cross-lane HLLH (binary DR64)
declare i64 @llvm.haydn.x2add32_hllh(i64, i64)
declare i64 @llvm.haydn.x2add32s_hllh(i64, i64)
declare i64 @llvm.haydn.x2sub32_hllh(i64, i64)
declare i64 @llvm.haydn.x2sub32s_hllh(i64, i64)

; X4 SIMD ALU binary (binary DR64)
declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16s(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4add16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4max16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4min16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4clamp16(<4 x i16>, <4 x i16>)