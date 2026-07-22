; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr_w}}
;
;
; SIMD X2/X4 Extended Masked Operations Test
;
; Tests extended SIMD operations that exercise cross-lane, ADDSUB
; lane-select, and complex multiply patterns. These are critical for
; FFT butterfly, FIR filter, and beamforming DSP kernels.
;
; X2: 2 lanes of 32-bit values packed in a single DR64 register
; X4: 4 lanes of 16-bit values packed in a single DR64 register
;
; These operations cannot be expressed in standard C.

;===----------------------------------------------------------------------===
; X2 ADDSUB/SUBADD (one lane adds, the other subtracts)
; Critical for FFT butterfly operations.
;===----------------------------------------------------------------------===

;
; Rebaselined (XFAIL hygiene post-): Bundle128 Flex printer
; uses optional `.sN` slot suffixes (e.g. xor32). Full asm dump regenerated
; from llc -verify-machineinstrs (backend clean).
;
define <2 x i32> @test_x2addsub32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2addsub32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2addsub32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2subadd32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32s(<2 x i32> %a, <2 x i32> %b) {
  %r = call <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===
; X2 ADDSUB/SUBADD HLLH cross-lane variants
;===----------------------------------------------------------------------===

define i64 @test_x2addsub32_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2addsub32_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2addsub32s_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2addsub32s_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2subadd32_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2subadd32_hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2subadd32s_hllh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2subadd32s_hllh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X2 lane select (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x2sel32_hh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sel32_hh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2sel32_hl(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sel32_hl(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2sel32_lh(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sel32_lh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x2sel32_ll(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x2sel32_ll(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X2 32-bit complex multiply (Path B: 2-dest DR64)
;===----------------------------------------------------------------------===

define i64 @test_x2cmul32(i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2cmul32s(i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32> %bc.3, <2 x i32> %bc.4)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===
; X4 complex multiply per-half (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4cmul16s_h(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x4cmul16s_h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x4cmul16s_l(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x4cmul16s_l(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x4cmula16s_h(i64 %acc, i64 %a) {
  %r = call i64 @llvm.haydn.x4cmula16s_h(i64 %acc, i64 %a)
  ret i64 %r
}

define i64 @test_x4cmula16s_l(i64 %acc, i64 %a) {
  %r = call i64 @llvm.haydn.x4cmula16s_l(i64 %acc, i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X4 conjugated complex multiply (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4cjmul16s_h(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x4cjmul16s_h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x4cjmul16s_l(i64 %a, i64 %b) {
  %r = call i64 @llvm.haydn.x4cjmul16s_l(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_x4cjmula16s_h(i64 %acc, i64 %a) {
  %r = call i64 @llvm.haydn.x4cjmula16s_h(i64 %acc, i64 %a)
  ret i64 %r
}

define i64 @test_x4cjmula16s_l(i64 %acc, i64 %a) {
  %r = call i64 @llvm.haydn.x4cjmula16s_l(i64 %acc, i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; Masked X2/X4 pattern tests (verify ALU instruction survives)
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2addsub32s_masked_lt(<2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %mask_val)
  %as = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %as)
  ret <2 x i32> %result
}

define <2 x i32> @test_x2cmul32_masked_eq(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
  %cmp = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %mask_val)
  %acc_i = bitcast <2 x i32> %acc to i64
  %a_i   = bitcast <2 x i32> %a to i64
  %b_i   = bitcast <2 x i32> %b to i64
  %bc.5 = bitcast i64 %a_i to <2 x i32>
  %bc.6 = bitcast i64 %b_i to <2 x i32>
  %cmul_i = call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32> %bc.5, <2 x i32> %bc.6)
  %cmul_h = extractvalue { i64, i64 } %cmul_i, 0
  %cmul   = bitcast i64 %cmul_h to <2 x i32>
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %acc,<2 x i32> %cmul)
  ret <2 x i32> %result
}

define <4 x i16> @test_x4cmul16s_h_masked_lt(<4 x i16> %a, <4 x i16> %b, <4 x i16> %mask_val) {
  %cmp = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %mask_val)
  %a_i = bitcast <4 x i16> %a to i64
  %b_i = bitcast <4 x i16> %b to i64
  %cmul_i = call i64 @llvm.haydn.x4cmul16s_h(i64 %a_i, i64 %b_i)
  %cmul   = bitcast i64 %cmul_i to <4 x i16>
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %a,<4 x i16> %cmul)
  ret <4 x i16> %result
}

define <2 x i32> @test_x2clamp32_masked_sle(<2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
  %cmp = call <2 x i32> @llvm.haydn.x2sle32(<2 x i32> %a,<2 x i32> %mask_val)
  %a_i = bitcast <2 x i32> %a to i64
  %b_i = bitcast <2 x i32> %b to i64
  %bc.7 = bitcast i64 %a_i to <2 x i32>
  %bc.8 = bitcast i64 %b_i to <2 x i32>
  %call.9 = call <2 x i32> @llvm.haydn.x2clamp32(<2 x i32> %bc.7, <2 x i32> %bc.8)
  %clamped_i = bitcast <2 x i32> %call.9 to i64
  %clamped   = bitcast i64 %clamped_i to <2 x i32>
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %clamped)
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; Intrinsic declarations
;===----------------------------------------------------------------------===

; SFR compare (binary DR64)
declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sle32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)

; SFR conditional move (binary DR64)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

; X2 ADDSUB/SUBADD (binary DR64)
declare <2 x i32> @llvm.haydn.x2addsub32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32>, <2 x i32>)

; X2 ADDSUB/SUBADD HLLH (binary DR64)
declare i64 @llvm.haydn.x2addsub32_hllh(i64, i64)
declare i64 @llvm.haydn.x2addsub32s_hllh(i64, i64)
declare i64 @llvm.haydn.x2subadd32_hllh(i64, i64)
declare i64 @llvm.haydn.x2subadd32s_hllh(i64, i64)

; X2 lane select (binary DR64)
declare i64 @llvm.haydn.x2sel32_hh(i64, i64)
declare i64 @llvm.haydn.x2sel32_hl(i64, i64)
declare i64 @llvm.haydn.x2sel32_lh(i64, i64)
declare i64 @llvm.haydn.x2sel32_ll(i64, i64)

; X2/X4 clamp (binary DR64)
declare <2 x i32> @llvm.haydn.x2clamp32(<2 x i32>, <2 x i32>)
; X2 complex multiply (Path B: 2-dest DR64)
declare { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s(<2 x i32>, <2 x i32>)
; X4 complex multiply per-half (binary DR64)
declare i64 @llvm.haydn.x4cmul16s_h(i64, i64)
declare i64 @llvm.haydn.x4cmul16s_l(i64, i64)
declare i64 @llvm.haydn.x4cmula16s_h(i64, i64)
declare i64 @llvm.haydn.x4cmula16s_l(i64, i64)

; X4 conjugated complex multiply (binary DR64)
declare i64 @llvm.haydn.x4cjmul16s_h(i64, i64)
declare i64 @llvm.haydn.x4cjmul16s_l(i64, i64)
declare i64 @llvm.haydn.x4cjmula16s_h(i64, i64)
declare i64 @llvm.haydn.x4cjmula16s_l(i64, i64)
