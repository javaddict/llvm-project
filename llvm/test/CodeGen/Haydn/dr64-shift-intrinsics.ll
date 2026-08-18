; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — DR64 shift intrinsics (full family).

; REGRESSION TEST: DR64 shift intrinsics (full family).
;
; HISTORY (resolved): this file was XFAIL'd since due
; to (a) a TEST-ONLY IR-signature bug - the X2/X4 SIMD register-form shift
; intrinsics (x2sll32/x2sra32/x2srl32/x4sll16/x4sra16/x4srl16/x2sra32r
; x4sra16r) are declared in IntrinsicsHaydn.td as taking/returning vector
; types (<2 x i32> / <4 x i16>) but were called in this test as `i64`. That
; produced "Intrinsic called with incompatible signature" before any backend
; code ran, masking the real codegen behavior. (b) A presumed gap-scope G12
; "x2mul32 family verifier error" that is no longer reproducible post
; (single Format E FlexMap slot authority + ALU64/MAC _S0
; variant retirement + ALU64 SIMD operand-modeling fixes). After fixing the
; IR signatures to match the td, the file compiles cleanly with
; verify-machineinstrs and every CHECK passes. XFAIL removed.
;
; Original XFAIL note also referenced "encoding gap G07" (instructions
; defined via HaydnInst without a Fmt* subclass get all-zero Inst encodings
; → MCID::Pseudo → AsmPrinter drops them). That is also no longer
; reproducible post-/ (the format-first tablegen cutover): every
; instruction now has a real Fmt* subclass, so the AsmPrinter emits real
; assembly for the full family tested here.

;===----------------------------------------------------------------------===;
; X2 SIMD register shifts (binary DR64) - WORKING, also in dr64-shift-regform.ll
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2sll32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2sra32(<2 x i32>, i32)
declare <2 x i32> @llvm.haydn.x2srl32(<2 x i32>, i32)

define dso_local <2 x i32> @test_x2sll32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sll32:
; CHECK: x2sll32
  %r = call <2 x i32> @llvm.haydn.x2sll32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2sra32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32:
; CHECK: x2sra32
  %r = call <2 x i32> @llvm.haydn.x2sra32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2srl32(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2srl32:
; CHECK: x2srl32
  %r = call <2 x i32> @llvm.haydn.x2srl32(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD register shifts (binary DR64) - WORKING, also in dr64-shift-regform.ll
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4sll16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4sra16(<4 x i16>, i32)
declare <4 x i16> @llvm.haydn.x4srl16(<4 x i16>, i32)

define dso_local <4 x i16> @test_x4sll16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sll16:
; CHECK: x4sll16
  %r = call <4 x i16> @llvm.haydn.x4sll16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4sra16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sra16:
; CHECK: x4sra16
  %r = call <4 x i16> @llvm.haydn.x4sra16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

define dso_local <4 x i16> @test_x4srl16(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4srl16:
; CHECK: x4srl16
  %r = call <4 x i16> @llvm.haydn.x4srl16(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2/X4 shift rounding (binary DR64) - WORKING, also in dr64-shift-regform.ll
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2sra32r(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4sra16r(<4 x i16>, i32)

define dso_local <2 x i32> @test_x2sra32r(<2 x i32> %a, i32 %b) {
; CHECK-LABEL: test_x2sra32r:
; CHECK: x2sra32r
  %r = call <2 x i32> @llvm.haydn.x2sra32r(<2 x i32> %a, i32 %b)
  ret <2 x i32> %r
}

define dso_local <4 x i16> @test_x4sra16r(<4 x i16> %a, i32 %b) {
; CHECK-LABEL: test_x4sra16r:
; CHECK: x4sra16r
  %r = call <4 x i16> @llvm.haydn.x4sra16r(<4 x i16> %a, i32 %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2 horizontal reductions (unary DR64) - STILL BROKEN (pseudo drop)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.x2hadd32.h(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32s.h(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32.l(<2 x i32>)
declare i64 @llvm.haydn.x2hadd32s.l(<2 x i32>)
define dso_local i64 @test_x2hadd32_h(i64 %a) {
; CHECK-LABEL: test_x2hadd32_h:
; CHECK: x2hadd32_h
  %bc.1 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32.h(<2 x i32> %bc.1)
  ret i64 %r
}

define dso_local i64 @test_x2hadd32s_h(i64 %a) {
; CHECK-LABEL: test_x2hadd32s_h:
; CHECK: x2hadd32s_h
  %bc.2 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32s.h(<2 x i32> %bc.2)
  ret i64 %r
}

define dso_local i64 @test_x2hadd32_l(i64 %a) {
; CHECK-LABEL: test_x2hadd32_l:
; CHECK: x2hadd32_l
  %bc.3 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32.l(<2 x i32> %bc.3)
  ret i64 %r
}

define dso_local i64 @test_x2hadd32s_l(i64 %a) {
; CHECK-LABEL: test_x2hadd32s_l:
; CHECK: x2hadd32s_l
  %bc.4 = bitcast i64 %a to <2 x i32>
  %r = call i64 @llvm.haydn.x2hadd32s.l(<2 x i32> %bc.4)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X4 horizontal reductions (unary DR64) - STILL BROKEN (pseudo drop)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.x4hadd16.h(<4 x i16>)
declare i64 @llvm.haydn.x4hadd16.l(<4 x i16>)
define dso_local i64 @test_x4hadd16_h(i64 %a) {
; CHECK-LABEL: test_x4hadd16_h:
; CHECK: x4hadd16_h
  %bc.5 = bitcast i64 %a to <4 x i16>
  %r = call i64 @llvm.haydn.x4hadd16.h(<4 x i16> %bc.5)
  ret i64 %r
}

define dso_local i64 @test_x4hadd16_l(i64 %a) {
; CHECK-LABEL: test_x4hadd16_l:
; CHECK: x4hadd16_l
  %bc.6 = bitcast i64 %a to <4 x i16>
  %r = call i64 @llvm.haydn.x4hadd16.l(<4 x i16> %bc.6)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2/X4 horizontal max/min - R_GD (DR64 src -> GPR32 scalar dst)
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.x2hmax32(<2 x i32>)
declare i32 @llvm.haydn.x2hmin32(<2 x i32>)
declare i32 @llvm.haydn.x4hmax16(<4 x i16>)
declare i32 @llvm.haydn.x4hmin16(<4 x i16>)
define dso_local i32 @test_x2hmax32(i64 %a) {
; CHECK-LABEL: test_x2hmax32:
; CHECK: x2hmax32
  %bc.7 = bitcast i64 %a to <2 x i32>
  %r = call i32 @llvm.haydn.x2hmax32(<2 x i32> %bc.7)
  ret i32 %r
}

define dso_local i32 @test_x2hmin32(i64 %a) {
; CHECK-LABEL: test_x2hmin32:
; CHECK: x2hmin32
  %bc.8 = bitcast i64 %a to <2 x i32>
  %r = call i32 @llvm.haydn.x2hmin32(<2 x i32> %bc.8)
  ret i32 %r
}

define dso_local i32 @test_x4hmax16(i64 %a) {
; CHECK-LABEL: test_x4hmax16:
; CHECK: x4hmax16
  %bc.9 = bitcast i64 %a to <4 x i16>
  %r = call i32 @llvm.haydn.x4hmax16(<4 x i16> %bc.9)
  ret i32 %r
}

define dso_local i32 @test_x4hmin16(i64 %a) {
; CHECK-LABEL: test_x4hmin16:
; CHECK: x4hmin16
  %bc.10 = bitcast i64 %a to <4 x i16>
  %r = call i32 @llvm.haydn.x4hmin16(<4 x i16> %bc.10)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; X2/X4 dot product (binary DR64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.x2dot32(<2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.x4dot16(<4 x i16>, <4 x i16>)
define dso_local i64 @test_x2dot32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2dot32:
; CHECK: x2dot32
  %bc.11 = bitcast i64 %a to <2 x i32>
  %bc.12 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.x2dot32(<2 x i32> %bc.11, <2 x i32> %bc.12)
  ret i64 %r
}

define dso_local i64 @test_x4dot16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4dot16:
; CHECK: x4dot16
  %bc.13 = bitcast i64 %a to <4 x i16>
  %bc.14 = bitcast i64 %b to <4 x i16>
  %r = call i64 @llvm.haydn.x4dot16(<4 x i16> %bc.13, <4 x i16> %bc.14)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2/X4 SIMD multiply (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2mul32(<2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x4mul16(<4 x i16>, <4 x i16>)
define dso_local i64 @test_x2mul32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mul32:
; CHECK: x2mul32
  %bc.15 = bitcast i64 %a to <2 x i32>
  %bc.16 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32> %bc.15, <2 x i32> %bc.16)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4mul16(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mul16:
; CHECK: x4mul16
  %bc.17 = bitcast i64 %a to <4 x i16>
  %bc.18 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mul16(<4 x i16> %bc.17, <4 x i16> %bc.18)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; X2/X4 SIMD MAC (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16s(i64, i64, <4 x i16>, <4 x i16>)
define dso_local i64 @test_x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mula32:
; CHECK: x2mula32
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, <2 x i32> %bc.19, <2 x i32> %bc.20)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2muls32:
; CHECK: x2muls32
  %bc.21 = bitcast i64 %a to <2 x i32>
  %bc.22 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, <2 x i32> %bc.21, <2 x i32> %bc.22)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16:
; CHECK: x4mula16
  %bc.23 = bitcast i64 %a to <4 x i16>
  %bc.24 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc, i64 %acc2, <4 x i16> %bc.23, <4 x i16> %bc.24)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4muls16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16:
; CHECK: x4muls16
  %bc.25 = bitcast i64 %a to <4 x i16>
  %bc.26 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16(i64 %acc, i64 %acc2, <4 x i16> %bc.25, <4 x i16> %bc.26)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16s:
; CHECK: x4mula16s
  %bc.27 = bitcast i64 %a to <4 x i16>
  %bc.28 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc, i64 %acc2, <4 x i16> %bc.27, <4 x i16> %bc.28)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16s:
; CHECK: x4muls16s
  %bc.29 = bitcast i64 %a to <4 x i16>
  %bc.30 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16s(i64 %acc, i64 %acc2, <4 x i16> %bc.29, <4 x i16> %bc.30)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; X2 multiply-pair variants (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2mulph32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulapl32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulsph32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2mulspl32(<2 x i32>, <2 x i32>, <2 x i32>)
define dso_local i64 @test_x2mulph32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulph32:
; CHECK: x2mulph32
  %bc.31 = bitcast i64 %a to <2 x i32>
  %bc.32 = bitcast i64 %b to <2 x i32>
  %call.33 = call <2 x i32> @llvm.haydn.x2mulph32(<2 x i32> %bc.31, <2 x i32> %bc.32)
  %r = bitcast <2 x i32> %call.33 to i64
  ret i64 %r
}

define dso_local i64 @test_x2mulpl32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulpl32:
; CHECK: x2mulpl32
  %bc.34 = bitcast i64 %a to <2 x i32>
  %bc.35 = bitcast i64 %b to <2 x i32>
  %call.36 = call <2 x i32> @llvm.haydn.x2mulpl32(<2 x i32> %bc.34, <2 x i32> %bc.35)
  %r = bitcast <2 x i32> %call.36 to i64
  ret i64 %r
}

define dso_local i64 @test_x2mulaph32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulaph32:
; CHECK: x2mulaph32
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2mulaph32(<2 x i32> %bc.1, <2 x i32> %bc.2, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

define dso_local i64 @test_x2mulapl32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulapl32:
; CHECK: x2mulapl32
  %bc.4 = bitcast i64 %a to <2 x i32>
  %bc.5 = bitcast i64 %b to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2mulapl32(<2 x i32> %bc.4, <2 x i32> %bc.5, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}

define dso_local i64 @test_x2mulsph32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulsph32:
; CHECK: x2mulsph32
  %bc.7 = bitcast i64 %a to <2 x i32>
  %bc.8 = bitcast i64 %b to <2 x i32>
  %call.9 = call <2 x i32> @llvm.haydn.x2mulsph32(<2 x i32> %bc.7, <2 x i32> %bc.8, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}

define dso_local i64 @test_x2mulspl32(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mulspl32:
; CHECK: x2mulspl32
  %bc.10 = bitcast i64 %a to <2 x i32>
  %bc.11 = bitcast i64 %b to <2 x i32>
  %call.12 = call <2 x i32> @llvm.haydn.x2mulspl32(<2 x i32> %bc.10, <2 x i32> %bc.11, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.12 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 32-bit complex fractional multiply (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmul32rss(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rss(<2 x i32>, <2 x i32>, <2 x i32>)
define dso_local i64 @test_x2fcmul32rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmul32rs:
; CHECK: x2fcmul32rs
  %bc.37 = bitcast i64 %a to <2 x i32>
  %bc.38 = bitcast i64 %b to <2 x i32>
  %call.39 = call <2 x i32> @llvm.haydn.x2fcmul32rs(<2 x i32> %bc.37, <2 x i32> %bc.38)
  %r = bitcast <2 x i32> %call.39 to i64
  ret i64 %r
}

define dso_local i64 @test_x2fcmul32rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmul32rss:
; CHECK: x2fcmul32rss
  %bc.40 = bitcast i64 %a to <2 x i32>
  %bc.41 = bitcast i64 %b to <2 x i32>
  %call.42 = call <2 x i32> @llvm.haydn.x2fcmul32rss(<2 x i32> %bc.40, <2 x i32> %bc.41)
  %r = bitcast <2 x i32> %call.42 to i64
  ret i64 %r
}

define dso_local i64 @test_x2fcmula32rs(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmula32rs:
; CHECK: x2fcmula32rs
  %bc.43 = bitcast i64 %acc to <2 x i32>
  %bc.44 = bitcast i64 %a to <2 x i32>
  %bc.45 = bitcast i64 %b to <2 x i32>
  %call.46 = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %bc.43, <2 x i32> %bc.44, <2 x i32> %bc.45)
  %r = bitcast <2 x i32> %call.46 to i64
  ret i64 %r
}

define dso_local i64 @test_x2fcmula32rss(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fcmula32rss:
; CHECK: x2fcmula32rss
  %bc.47 = bitcast i64 %acc to <2 x i32>
  %bc.48 = bitcast i64 %a to <2 x i32>
  %bc.49 = bitcast i64 %b to <2 x i32>
  %call.50 = call <2 x i32> @llvm.haydn.x2fcmula32rss(<2 x i32> %bc.47, <2 x i32> %bc.48, <2 x i32> %bc.49)
  %r = bitcast <2 x i32> %call.50 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; X2 FF2 shift variants (binary DR64)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2ff2rsst32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2ff2rst32(<2 x i32>, <2 x i32>)

define dso_local <2 x i32> @test_x2ff2rsst32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2ff2rsst32:
; CHECK: x2ff2rsst32
  %r = call <2 x i32> @llvm.haydn.x2ff2rsst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define dso_local <2 x i32> @test_x2ff2rst32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2ff2rst32:
; CHECK: x2ff2rst32
  %r = call <2 x i32> @llvm.haydn.x2ff2rst32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 FF2MUL (ternary DR64)
;===----------------------------------------------------------------------===;

declare { i64, i64 } @llvm.haydn.x4ff2mul16s(<4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4ff2mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4ff2muls16s(i64, i64, <4 x i16>, <4 x i16>)
define dso_local i64 @test_x4ff2mul16s(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4ff2mul16s:
; CHECK: x4ff2mul16s
  %bc.51 = bitcast i64 %a to <4 x i16>
  %bc.52 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2mul16s(<4 x i16> %bc.51, <4 x i16> %bc.52)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4ff2mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4ff2mula16s:
; CHECK: x4ff2mula16s
  %bc.53 = bitcast i64 %a to <4 x i16>
  %bc.54 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2mula16s(i64 %acc, i64 %acc2, <4 x i16> %bc.53, <4 x i16> %bc.54)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define dso_local i64 @test_x4ff2muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4ff2muls16s:
; CHECK: x4ff2muls16s
  %bc.55 = bitcast i64 %a to <4 x i16>
  %bc.56 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4ff2muls16s(i64 %acc, i64 %acc2, <4 x i16> %bc.55, <4 x i16> %bc.56)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===;
; X2/X4 immediate shifts - commented out (CANNOT SELECT)
; FIXME: These intrinsics need ISel patterns added to HaydnIntrinsics.td.
; The cross-type signature (i64, i32) -> i64 requires special ISel handling.
; When ISel patterns are added, uncomment and test:
; x2slli32, x2srai32, x2srli32, x4slli16, x4srai16, x4srli16
;===----------------------------------------------------------------------===;
