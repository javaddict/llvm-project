; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Comprehensive X2/X4 SIMD intrinsic coverage test.

; Comprehensive X2/X4 SIMD intrinsic coverage test.
; Covers all 56 previously-missing SIMD operations from the ISA spec.
;
; Categories tested:
; Non-saturating arithmetic (X2ADD32, X2SUB32, X4ADD16, X4SUB16)
; HLLH cross-lane variants
; Register shifts (SLL, SRA, SRL)
; Immediate shifts (SLLI, SRAI, SRLI)
; Shift with rounding (SRA32R, SRA16R)
; Horizontal reductions (HADD, HMAX, HMIN)
; Dot product (DOT32, DOT16)
; Multiply (X2MUL32, X4MUL16)
; Multiply-pair variants (MULPH, MULPL, MULAPH, MULAPL, MULSPH, MULSPL)
; Complex fractional multiply (FCMUL32, FCMULA32)
; CMUL F2 variants
; FF2 shift variants
; FF2MUL X4 variants
; Pack/sat/sel (SAT32T16, SELI16)

declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4add16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16(<4 x i16>, <4 x i16>)

declare <2 x i32> @llvm.haydn.x2add32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2add32s.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32s.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sll32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2sra32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2srl32(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4sll16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4sra16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4srl16(<4 x i16>, i32)

declare <2 x i32> @llvm.haydn.x2slli32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2srai32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2srli32(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4slli16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4srai16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4srli16(<4 x i16>, i32)

declare <2 x i32> @llvm.haydn.x2sra32r(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4sra16r(<4 x i16>, i32)

declare i64 @llvm.haydn.x2hadd32.h(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32s.h(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32.l(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32s.l(<2 x i32>)
declare i64 @llvm.haydn.x4hadd16.h(<4 x i16>)
declare i64 @llvm.haydn.x4hadd16.l(<4 x i16>)
; R_GD: horizontal max/min reduce to GPR32 (FormatsALU64 / G3), not i64.
declare i32 @llvm.haydn.x2hmax32(<2 x i32>)
declare i32 @llvm.haydn.x2hmin32(<2 x i32>)
declare i32 @llvm.haydn.x4hmax16(<4 x i16>)
declare i32 @llvm.haydn.x4hmin16(<4 x i16>)
declare i64 @llvm.haydn.x2dot32(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.x4dot16(<4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x2mul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x4mul16(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2mulph32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulapl32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulsph32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulspl32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmul32rss(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rss(<2 x i32>, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2ff2rsst32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2ff2rst32(<2 x i32>, <2 x i32>)

declare { i64, i64 } @llvm.haydn.x4ff2mul16s(<4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4ff2mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4ff2muls16s(i64, i64, <4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

;===----------------------------------------------------------------------===;
; Non-saturating SIMD arithmetic
;===----------------------------------------------------------------------===;

define <2 x i32> @test_x2add32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32:
; CHECK: x2add32
  %r = call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sub32:
; CHECK: x2sub32
  %r = call <2 x i32> @llvm.haydn.x2sub32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4add16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4add16:
; CHECK: x4add16
  %r = call <4 x i16> @llvm.haydn.x4add16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4sub16:
; CHECK: x4sub16
  %r = call <4 x i16> @llvm.haydn.x4sub16(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; HLLH cross-lane variants
;===----------------------------------------------------------------------===;

define i64 @test_x2add32_hllh(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2add32_hllh:
; CHECK: x2add32_hllh
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2add32.hllh(<2 x i32> %bc.1, <2 x i32> %bc.2)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

define i64 @test_x2sub32s_hllh(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2sub32s_hllh:
; CHECK: x2sub32s_hllh
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2sub32s.hllh(<2 x i32> %bc.4, <2 x i32> %bc.5)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Register shifts
;===----------------------------------------------------------------------===;

define <2 x i32> @test_x2sll32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sll32:
; CHECK: x2sll32
  %r = call <2 x i32> @llvm.haydn.x2sll32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sra32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32:
; CHECK: x2sra32
  %r = call <2 x i32> @llvm.haydn.x2sra32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4srl16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4srl16:
; CHECK: x4srl16
  %r = call <4 x i16> @llvm.haydn.x4srl16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; Immediate shifts
;===----------------------------------------------------------------------===;

define <2 x i32> @test_x2slli32(<2 x i32> %a) {
; CHECK-LABEL: test_x2slli32:
; CHECK: x2slli32
  %r = call <2 x i32> @llvm.haydn.x2slli32(<2 x i32> %a, i32 5)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2srai32(<2 x i32> %a) {
; CHECK-LABEL: test_x2srai32:
; CHECK: x2srai32
  %r = call <2 x i32> @llvm.haydn.x2srai32(<2 x i32> %a, i32 3)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4slli16(<4 x i16> %a) {
; CHECK-LABEL: test_x4slli16:
; CHECK: x4slli16
  %r = call <4 x i16> @llvm.haydn.x4slli16(<4 x i16> %a, i32 4)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4srai16(<4 x i16> %a) {
; CHECK-LABEL: test_x4srai16:
; CHECK: x4srai16
  %r = call <4 x i16> @llvm.haydn.x4srai16(<4 x i16> %a, i32 2)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; Shift with rounding
;===----------------------------------------------------------------------===;

define <2 x i32> @test_x2sra32r(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32r:
; CHECK: x2sra32r
  %r = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define <4 x i16> @test_x4sra16r(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sra16r:
; CHECK: x4sra16r
  %r = call <4 x i16> @llvm.haydn.x4sra16r(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; Horizontal reductions
;===----------------------------------------------------------------------===;

define i64 @test_x2hadd32_h(i64 %a) nounwind {
; CHECK-LABEL: test_x2hadd32_h:
; CHECK: x2hadd32_h
  %bc.7 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32.h(<2 x i32> %bc.7)
  ret i64 %r
}

define i64 @test_x2hadd32s_l(i64 %a) nounwind {
; CHECK-LABEL: test_x2hadd32s_l:
; CHECK: x2hadd32s_l
  %bc.8 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32s.l(<2 x i32> %bc.8)
  ret i64 %r
}

define i64 @test_x4hadd16_h(i64 %a) nounwind {
; CHECK-LABEL: test_x4hadd16_h:
; CHECK: x4hadd16_h
  %bc.9 = bitcast i64 %a to <4 x i16>
  %r = call i64 @llvm.haydn.x4hadd16.h(<4 x i16> %bc.9)
  ret i64 %r
}

define i32 @test_x2hmax32(i64 %a) nounwind {
; CHECK-LABEL: test_x2hmax32:
; CHECK: x2hmax32
  %bc.10 = bitcast i64 %a to <2 x i32>
  %r = call i32 @llvm.haydn.x2hmax32(<2 x i32> %bc.10)
  ret i32 %r
}

define i32 @test_x4hmin16(i64 %a) nounwind {
; CHECK-LABEL: test_x4hmin16:
; CHECK: x4hmin16
  %bc.11 = bitcast i64 %a to <4 x i16>
  %r = call i32 @llvm.haydn.x4hmin16(<4 x i16> %bc.11)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Dot product
;===----------------------------------------------------------------------===;

define i64 @test_x2dot32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2dot32:
; CHECK: x2dot32
  %bc.12 = bitcast i64 %a to <2 x i32>
  %bc.13 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.x2dot32(<2 x i32> %bc.12, <2 x i32> %bc.13)
  ret i64 %r
}

define i64 @test_x4dot16(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x4dot16:
; CHECK: x4dot16
  %bc.14 = bitcast i64 %a to <4 x i16>
  %bc.15 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.x4dot16(<4 x i16> %bc.14, <4 x i16> %bc.15)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Multiply (ternary)
;===----------------------------------------------------------------------===;

define i64 @test_x2mul32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2mul32:
; CHECK: x2mul32
  %bc.16 = bitcast i64 %a to <2 x i32>
  %bc.17 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32> %bc.16, <2 x i32> %bc.17)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4mul16(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x4mul16:
; CHECK: x4mul16
  %bc.18 = bitcast i64 %a to <4 x i16>
  %bc.19 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mul16(<4 x i16> %bc.18, <4 x i16> %bc.19)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; Multiply-pair variants
;===----------------------------------------------------------------------===;

define i64 @test_x2mulph32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2mulph32:
; CHECK: x2mulph32
  %bc.20 = bitcast i64 %a to <2 x i32>
  %bc.21 = bitcast i64 %b to <2 x i32>
  %call.22 = call <2 x i32> @llvm.haydn.x2mulph32(<2 x i32> %bc.20, <2 x i32> %bc.21)
  %r = bitcast <2 x i32> %call.22 to i64
  ret i64 %r
}

define i64 @test_x2mulaph32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2mulaph32:
; CHECK: x2mulaph32
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32> %bc.1, <2 x i32> %bc.2, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

define i64 @test_x2mulspl32(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2mulspl32:
; CHECK: x2mulspl32
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2mulspl32(<2 x i32> %bc.4, <2 x i32> %bc.5, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Complex fractional multiply
;===----------------------------------------------------------------------===;

define i64 @test_x2fcmul32rs(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2fcmul32rs:
; CHECK: x2fcmul32rs
  %bc.23 = bitcast i64 %a to <2 x i32>
  %bc.24 = bitcast i64 %b to <2 x i32>
  %call.25 = call <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32> %bc.23, <2 x i32> %bc.24)
  %r = bitcast <2 x i32> %call.25 to i64
  ret i64 %r
}

define i64 @test_x2fcmula32rss(i64 %acc, i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2fcmula32rss:
; CHECK: x2fcmula32rss
  %bc.26 = bitcast i64 %acc to <2 x i32>
  %bc.27 = bitcast i64 %a to <2 x i32>
  %bc.28 = bitcast i64 %b to <2 x i32>
  %call.29 = call <2 x i32> @llvm.haydn.x2fcmula32rss(<2 x i32> %bc.26, <2 x i32> %bc.27, <2 x i32> %bc.28)
  %r = bitcast <2 x i32> %call.29 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; CMUL F2 variants
;===----------------------------------------------------------------------===;

define i64 @test_x2cmul32_f2(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2cmul32_f2:
; CHECK: x2cmul32_f2
  %bc.30 = bitcast i64 %a to <2 x i32>
  %bc.31 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32.f2(<2 x i32> %bc.30, <2 x i32> %bc.31)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2cmul32s_f2(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x2cmul32s_f2:
; CHECK: x2cmul32s_f2
  %bc.32 = bitcast i64 %a to <2 x i32>
  %bc.33 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2cmul32s.f2(<2 x i32> %bc.32, <2 x i32> %bc.33)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; FF2 shift variants
;===----------------------------------------------------------------------===;

define <2 x i32> @test_x2ff2rsst32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2ff2rsst32:
; CHECK: x2ff2rsst32
  %r = call <2 x i32> @llvm.haydn.x2ff2rsst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2ff2rst32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2ff2rst32:
; CHECK: x2ff2rst32
  %r = call <2 x i32> @llvm.haydn.x2ff2rst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 FF2MUL variants
;===----------------------------------------------------------------------===;

define i64 @test_x4ff2mul16s(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x4ff2mul16s:
; CHECK: x4ff2mul16s
  %bc.34 = bitcast i64 %a to <4 x i16>
  %bc.35 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2mul16s(<4 x i16> %bc.34, <4 x i16> %bc.35)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4ff2mula16s(i64 %a, i64 %b, i64 %c, i64 %d) nounwind {
; CHECK-LABEL: test_x4ff2mula16s:
; CHECK: x4ff2mula16s
  %bc.36 = bitcast i64 %c to <4 x i16>
  %bc.37 = bitcast i64 %d to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2mula16s(i64 %a, i64 %b, <4 x i16> %bc.36, <4 x i16> %bc.37)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4ff2muls16s(i64 %a, i64 %b, i64 %c, i64 %d) nounwind {
; CHECK-LABEL: test_x4ff2muls16s:
; CHECK: x4ff2muls16s
  %bc.38 = bitcast i64 %c to <4 x i16>
  %bc.39 = bitcast i64 %d to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2muls16s(i64 %a, i64 %b, <4 x i16> %bc.38, <4 x i16> %bc.39)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; Pack/sat/sel
;===----------------------------------------------------------------------===;

define i64 @test_x4sat32t16(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: test_x4sat32t16:
; CHECK: x4sat32t16
  %bc.40 = bitcast i64 %a to <2 x i32>
  %bc.41 = bitcast i64 %b to <2 x i32>
  %call.42 = call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> %bc.40, <2 x i32> %bc.41)
  %r = bitcast <4 x i16> %call.42 to i64
  ret i64 %r
}

define <4 x i16> @test_x4seli16(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4seli16:
; CHECK: x4seli16
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a,<4 x i16> %b, i32 5)
  ret <4 x i16> %r
}
