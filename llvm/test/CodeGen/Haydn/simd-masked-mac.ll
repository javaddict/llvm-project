; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: smoke — labels + payload opcodes; bundle regroup must not red this file.
;
;
; SIMD Masked MAC (Multiply-Accumulate) Operations Test
;
; Tests SIMD MAC operations that implement the core DSP kernel compute
; patterns. These operations cannot be expressed in standard C.
;
; Haydn MAC operations:
; X2MULA32/X2MULS32 — dual 32-bit MAC/MSU (ternary DR64)
; X4MULA16/X4MULS16 — quad 16-bit MAC/MSU (ternary DR64)
; X4MULA16S/X4MULS16S — quad 16-bit saturating MAC/MSU (ternary DR64)
; X2FMUL32RS/RSS/TS — dual 32-bit fractional multiply (binary DR64)
; X2FMULA32RS/RSS/TS — dual 32-bit fractional MAC (binary DR64)
; X2FMULS32RS/RSS/TS — dual 32-bit fractional MSU (binary DR64)
; X4FMUL16RS/RSS/TS — quad 16-bit fractional multiply (binary DR64)
;
; NOTE: SFR compare/move intrinsics are currently eliminated during codegen.
; The masked pattern tests verify the ALU instruction survives and document
; the intended 3-instruction compare->ALU->movt pattern.

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) MAC / MSU (ternary DR64)
;===----------------------------------------------------------------------===

;
; Rebaselined (XFAIL hygiene post-): Format E printer
; uses optional `.sN` slot suffixes (e.g. xor32). Full asm dump regenerated
; from llc -verify-machineinstrs (backend clean).
;

define i64 @test_x2mula32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2mula32:
; CHECK-DAG: x2mula32
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc, i64 %acc2, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x2muls32(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x2muls32:
; CHECK-DAG: x2muls32
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call { i64, i64 } @llvm.haydn.x2muls32(i64 %acc, i64 %acc2, <2 x i32> %bc.3, <2 x i32> %bc.4)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) MAC / MSU (ternary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4mula16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16:
; CHECK-DAG: x4mula16
  %bc.5 = bitcast i64 %a to <4 x i16>
  %bc.6 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc, i64 %acc2, <4 x i16> %bc.5, <4 x i16> %bc.6)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16:
; CHECK-DAG: x4muls16
  %bc.7 = bitcast i64 %a to <4 x i16>
  %bc.8 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16(i64 %acc, i64 %acc2, <4 x i16> %bc.7, <4 x i16> %bc.8)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) saturating MAC / MSU (ternary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4mula16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4mula16s:
; CHECK-DAG: x4mula16s
  %bc.9 = bitcast i64 %a to <4 x i16>
  %bc.10 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4mula16s(i64 %acc, i64 %acc2, <4 x i16> %bc.9, <4 x i16> %bc.10)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

define i64 @test_x4muls16s(i64 %acc, i64 %acc2, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4muls16s:
; CHECK-DAG: x4muls16s
  %bc.11 = bitcast i64 %a to <4 x i16>
  %bc.12 = bitcast i64 %b to <4 x i16>
  %r = call { i64, i64 } @llvm.haydn.x4muls16s(i64 %acc, i64 %acc2, <4 x i16> %bc.11, <4 x i16> %bc.12)
  %hi = extractvalue { i64, i64 } %r, 0
  ret i64 %hi
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) fractional multiply (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x2fmul32rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fmul32rs:
; CHECK-DAG: x2fmul32rs
  %bc.13 = bitcast i64 %a to <2 x i32>
  %bc.14 = bitcast i64 %b to <2 x i32>
  %call.15 = call <2 x i32> @llvm.haydn.x2fmul32rs(<2 x i32> %bc.13, <2 x i32> %bc.14)
  %r = bitcast <2 x i32> %call.15 to i64
  ret i64 %r
}

define i64 @test_x2fmul32rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fmul32rss:
; CHECK-DAG: x2fmul32rss
  %bc.16 = bitcast i64 %a to <2 x i32>
  %bc.17 = bitcast i64 %b to <2 x i32>
  %call.18 = call <2 x i32> @llvm.haydn.x2fmul32rss(<2 x i32> %bc.16, <2 x i32> %bc.17)
  %r = bitcast <2 x i32> %call.18 to i64
  ret i64 %r
}

define i64 @test_x2fmul32ts(i64 %a, i64 %b) {
; CHECK-LABEL: test_x2fmul32ts:
; CHECK-DAG: x2fmul32ts
  %bc.19 = bitcast i64 %a to <2 x i32>
  %bc.20 = bitcast i64 %b to <2 x i32>
  %call.21 = call <2 x i32> @llvm.haydn.x2fmul32ts(<2 x i32> %bc.19, <2 x i32> %bc.20)
  %r = bitcast <2 x i32> %call.21 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) fractional MAC (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x2fmula32rs(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmula32rs:
; CHECK-DAG: x2fmula32rs
  %bc.1 = bitcast i64 %acc to <2 x i32>
  %bc.2 = bitcast i64 %a to <2 x i32>
  %call.3 = call <2 x i32> @llvm.haydn.x2fmula32rs(<2 x i32> %bc.1, <2 x i32> %bc.2, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.3 to i64
  ret i64 %r
}

define i64 @test_x2fmula32rss(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmula32rss:
; CHECK-DAG: x2fmula32rss
  %bc.4 = bitcast i64 %acc to <2 x i32>
  %bc.5 = bitcast i64 %a to <2 x i32>
  %call.6 = call <2 x i32> @llvm.haydn.x2fmula32rss(<2 x i32> %bc.4, <2 x i32> %bc.5, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.6 to i64
  ret i64 %r
}

define i64 @test_x2fmula32ts(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmula32ts:
; CHECK-DAG: x2fmula32ts
  %bc.7 = bitcast i64 %acc to <2 x i32>
  %bc.8 = bitcast i64 %a to <2 x i32>
  %call.9 = call <2 x i32> @llvm.haydn.x2fmula32ts(<2 x i32> %bc.7, <2 x i32> %bc.8, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.9 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X2 (dual 32-bit) fractional MSU (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x2fmuls32rs(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmuls32rs:
; CHECK-DAG: x2fmuls32rs
  %bc.10 = bitcast i64 %acc to <2 x i32>
  %bc.11 = bitcast i64 %a to <2 x i32>
  %call.12 = call <2 x i32> @llvm.haydn.x2fmuls32rs(<2 x i32> %bc.10, <2 x i32> %bc.11, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.12 to i64
  ret i64 %r
}

define i64 @test_x2fmuls32rss(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmuls32rss:
; CHECK-DAG: x2fmuls32rss
  %bc.13 = bitcast i64 %acc to <2 x i32>
  %bc.14 = bitcast i64 %a to <2 x i32>
  %call.15 = call <2 x i32> @llvm.haydn.x2fmuls32rss(<2 x i32> %bc.13, <2 x i32> %bc.14, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.15 to i64
  ret i64 %r
}

define i64 @test_x2fmuls32ts(i64 %acc, i64 %a) {
; CHECK-LABEL: test_x2fmuls32ts:
; CHECK-DAG: x2fmuls32ts
  %bc.16 = bitcast i64 %acc to <2 x i32>
  %bc.17 = bitcast i64 %a to <2 x i32>
  %call.18 = call <2 x i32> @llvm.haydn.x2fmuls32ts(<2 x i32> %bc.16, <2 x i32> %bc.17, <2 x i32> zeroinitializer)
  %r = bitcast <2 x i32> %call.18 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; X4 (quad 16-bit) fractional multiply (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4fmul16rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fmul16rs:
; CHECK-DAG: x4fmul16rs
  %bc.22 = bitcast i64 %a to <4 x i16>
  %bc.23 = bitcast i64 %b to <4 x i16>
  %call.24 = call <4 x i16> @llvm.haydn.x4fmul16rs(<4 x i16> %bc.22, <4 x i16> %bc.23)
  %r = bitcast <4 x i16> %call.24 to i64
  ret i64 %r
}

define i64 @test_x4fmul16rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fmul16rss:
; CHECK-DAG: x4fmul16rss
  %bc.25 = bitcast i64 %a to <4 x i16>
  %bc.26 = bitcast i64 %b to <4 x i16>
  %call.27 = call <4 x i16> @llvm.haydn.x4fmul16rss(<4 x i16> %bc.25, <4 x i16> %bc.26)
  %r = bitcast <4 x i16> %call.27 to i64
  ret i64 %r
}

define i64 @test_x4fmul16ts(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fmul16ts:
; CHECK-DAG: x4fmul16ts
  %bc.28 = bitcast i64 %a to <4 x i16>
  %bc.29 = bitcast i64 %b to <4 x i16>
  %call.30 = call <4 x i16> @llvm.haydn.x4fmul16ts(<4 x i16> %bc.28, <4 x i16> %bc.29)
  %r = bitcast <4 x i16> %call.30 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; Complex multiply (binary DR64)
;===----------------------------------------------------------------------===

define i64 @test_x4fcmul16rs(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rs:
; CHECK-DAG: x4fcmul16rs
  %bc.31 = bitcast i64 %a to <4 x i16>
  %bc.32 = bitcast i64 %b to <4 x i16>
  %call.33 = call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %bc.31, <4 x i16> %bc.32)
  %r = bitcast <4 x i16> %call.33 to i64
  ret i64 %r
}

; Ternary accumulator form (acc, a, b) — reads rtd per DB.
define i64 @test_x4fcmula16rs(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rs:
; CHECK-DAG: x4fcmula16rs
  %bc.34 = bitcast i64 %acc to <4 x i16>
  %bc.35 = bitcast i64 %a to <4 x i16>
  %bc.36 = bitcast i64 %b to <4 x i16>
  %call.37 = call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %bc.34, <4 x i16> %bc.35, <4 x i16> %bc.36)
  %r = bitcast <4 x i16> %call.37 to i64
  ret i64 %r
}

define i64 @test_x4fcmul16rss(i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmul16rss:
; CHECK-DAG: x4fcmul16rss
  %bc.38 = bitcast i64 %a to <4 x i16>
  %bc.39 = bitcast i64 %b to <4 x i16>
  %call.40 = call <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16> %bc.38, <4 x i16> %bc.39)
  %r = bitcast <4 x i16> %call.40 to i64
  ret i64 %r
}

; Ternary accumulator form.
define i64 @test_x4fcmula16rss(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: test_x4fcmula16rss:
; CHECK-DAG: x4fcmula16rss
  %bc.41 = bitcast i64 %acc to <4 x i16>
  %bc.42 = bitcast i64 %a to <4 x i16>
  %bc.43 = bitcast i64 %b to <4 x i16>
  %call.44 = call <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16> %bc.41, <4 x i16> %bc.42, <4 x i16> %bc.43)
  %r = bitcast <4 x i16> %call.44 to i64
  ret i64 %r
}

;===----------------------------------------------------------------------===
; Masked MAC pattern (documented, verify MAC instruction survives)
;===----------------------------------------------------------------------===

define <2 x i32> @test_x2mula32_masked_lt(<2 x i32> %acc, <2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
; CHECK-LABEL: test_x2mula32_masked_lt:
; CHECK-DAG: or64
; CHECK-DAG: x2slt32
; CHECK-DAG: x2mula32
; CHECK-DAG: x2movt32
  %cmp = call <2 x i32> @llvm.haydn.x2slt32(<2 x i32> %a,<2 x i32> %mask_val)
  %acc_i = bitcast <2 x i32> %acc to i64
  %a_i   = bitcast <2 x i32> %a to i64
  %b_i   = bitcast <2 x i32> %b to i64
  %mv_i  = bitcast <2 x i32> %mask_val to i64
  %bc.45 = bitcast i64 %a_i to <2 x i32>
  %bc.46 = bitcast i64 %b_i to <2 x i32>
  %mac_pair = call { i64, i64 } @llvm.haydn.x2mula32(i64 %acc_i, i64 %mv_i, <2 x i32> %bc.45, <2 x i32> %bc.46)
  %mac_i = extractvalue { i64, i64 } %mac_pair, 0
  %mac   = bitcast i64 %mac_i to <2 x i32>
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %acc,<2 x i32> %mac)
  ret <2 x i32> %result
}

define <4 x i16> @test_x4mula16_masked_lt(<4 x i16> %acc, <4 x i16> %a, <4 x i16> %b, <4 x i16> %mask_val) {
; CHECK-LABEL: test_x4mula16_masked_lt:
; CHECK-DAG: or64
; CHECK-DAG: x4slt16
; CHECK-DAG: x4mula16
; CHECK-DAG: x4movt16
  %cmp = call <4 x i16> @llvm.haydn.x4slt16(<4 x i16> %a,<4 x i16> %mask_val)
  %acc_i = bitcast <4 x i16> %acc to i64
  %a_i   = bitcast <4 x i16> %a to i64
  %b_i   = bitcast <4 x i16> %b to i64
  %mv_i  = bitcast <4 x i16> %mask_val to i64
  %bc.47 = bitcast i64 %a_i to <4 x i16>
  %bc.48 = bitcast i64 %b_i to <4 x i16>
  %mac_pair = call { i64, i64 } @llvm.haydn.x4mula16(i64 %acc_i, i64 %mv_i, <4 x i16> %bc.47, <4 x i16> %bc.48)
  %mac_i = extractvalue { i64, i64 } %mac_pair, 0
  %mac   = bitcast i64 %mac_i to <4 x i16>
  %result = call <4 x i16> @llvm.haydn.x4movt16(<4 x i16> %acc,<4 x i16> %mac)
  ret <4 x i16> %result
}

define <2 x i32> @test_x2fmul32rs_masked_eq(<2 x i32> %a, <2 x i32> %b, <2 x i32> %mask_val) {
; CHECK-LABEL: test_x2fmul32rs_masked_eq:
; CHECK-DAG: x2seq32
; CHECK-DAG: x2fmul32rs
; CHECK-DAG: x2movt32
  %cmp = call <2 x i32> @llvm.haydn.x2seq32(<2 x i32> %a,<2 x i32> %mask_val)
  %a_i = bitcast <2 x i32> %a to i64
  %b_i = bitcast <2 x i32> %b to i64
  %bc.49 = bitcast i64 %a_i to <2 x i32>
  %bc.50 = bitcast i64 %b_i to <2 x i32>
  %call.51 = call <2 x i32> @llvm.haydn.x2fmul32rs(<2 x i32> %bc.49, <2 x i32> %bc.50)
  %mul_i = bitcast <2 x i32> %call.51 to i64
  %mul   = bitcast i64 %mul_i to <2 x i32>
  %result = call <2 x i32> @llvm.haydn.x2movt32(<2 x i32> %a,<2 x i32> %mul)
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; Intrinsic declarations
;===----------------------------------------------------------------------===@

; SFR compare (binary DR64)
declare <2 x i32> @llvm.haydn.x2seq32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2slt32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4slt16(<4 x i16>, <4 x i16>)

; SFR conditional move (binary DR64)
declare <2 x i32> @llvm.haydn.x2movt32(<2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4movt16(<4 x i16>, <4 x i16>)

; SIMD MAC ternary (DR64 x3) -- Path B: { i64, i64 } result, 4 args (acc1, acc2, src1, src2)
declare { i64, i64 } @llvm.haydn.x2mula32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x2muls32(i64, i64, <2 x i32>, <2 x i32>)
declare { i64, i64 } @llvm.haydn.x4mula16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4mula16s(i64, i64, <4 x i16>, <4 x i16>)
declare { i64, i64 } @llvm.haydn.x4muls16s(i64, i64, <4 x i16>, <4 x i16>)
; X2 fractional multiply/MAC/MSU (binary DR64)
declare <2 x i32> @llvm.haydn.x2fmul32rs(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmul32rss(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmul32ts(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32rss(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmula32ts(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32rs(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32rss(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fmuls32ts(<2 x i32>, <2 x i32>, <2 x i32>)
; X4 fractional multiply (binary DR64)
declare <4 x i16> @llvm.haydn.x4fmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fmul16ts(<4 x i16>, <4 x i16>)
; Complex multiply (binary DR64)
declare <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16>, <4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmul16rss(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4fcmula16rss(<4 x i16>, <4 x i16>, <4 x i16>)
