; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — FILED : DR64 R_CMP instructions (SLT64 SEQ64, SLE64 and _S0/_S2 variants) have an operand-flag.

; REGRESSION FILED : DR64 R_CMP instructions (SLT64
; SEQ64, SLE64 and _S0/_S2 variants) have an operand-flag
; bug in HaydnFormatsALU64.td that aborts the MachineVerifier ("Explicit
; operand marked as def"). The.td marks operand 0 ($rd) as a def with
; hasSideEffects = 1 (implicit SFR write) — the verifier rejects this
; combination. Regressed in the Flex cutover. Selector is correct
; (MIR shows real emission); only the.td operand flags need adjusting.
; Track under / Flex cutover (DR64 R_CMP operand flags). Do NOT
; rebaseline CHECKs to silence the verifier abort.
; Updated for native DR64 shift (sll64/srl64/sra64)
;
; Comprehensive DR64 ALU intrinsics test for Haydn backend.
; Tests scalar 64-bit ALU operations, saturating arithmetic, shifts
; and dual-32-bit SIMD arithmetic that use DR64 register operands.
;
; Categories:
; 64-bit add/sub variants (add64_h, add64_l, sub64_h, sub64_l)
; 64-bit saturating add/sub (add64s, sub64s)
; 64-bit saturating abs/neg (abs64s, neg64s, abs64, neg64)
; 64-bit bitwise (not64, seq64)
; 64-bit min/max (max64, min64)
; 64-bit multiply/subtract dual (mulsa32, mulss32)
; Cross-type shift (sll64, sra64, srl64, sra64r)
; Pack/shift-round composite (satsr64, packsr32)
; X2 SIMD non-saturating (x2add32, x2sub32)
; X2 SIMD saturating (x2add32s, x2sub32s)
; X2 SIMD addsub (x2addsub32, x2addsub32s)
; X4 SIMD non-saturating (x4add16, x4sub16)
; X4 SIMD saturating (x4add16s, x4sub16s)
; X2 HLLH cross-lane (x2add32_hllh, x2sub32_hllh, etc.)
; X2 addsub/subadd cross-lane variants

;===----------------------------------------------------------------------===;
; 64-bit add/sub variants (non-saturating)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64.h(i64, i64)
declare i64 @llvm.haydn.add64.l(i64, i64)
declare i64 @llvm.haydn.sub64.h(i64, i64)
declare i64 @llvm.haydn.sub64.l(i64, i64)

define i64 @test_add64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_h:
; CHECK: add64_h
  %r = call i64 @llvm.haydn.add64.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_add64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64_l:
; CHECK: add64_l
  %r = call i64 @llvm.haydn.add64.l(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64_h(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_h:
; CHECK: sub64_h
  %r = call i64 @llvm.haydn.sub64.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64_l(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64_l:
; CHECK: sub64_l
  %r = call i64 @llvm.haydn.sub64.l(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit saturating add/sub
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.add64s(i64, i64)
declare i64 @llvm.haydn.sub64s(i64, i64)

define i64 @test_add64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64s:
; CHECK: add64s
  %r = call i64 @llvm.haydn.add64s(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_sub64s(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64s:
; CHECK: sub64s
  %r = call i64 @llvm.haydn.sub64s(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit saturating abs/neg
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.abs64s(i64)
declare i64 @llvm.haydn.neg64s(i64)
declare i64 @llvm.haydn.abs64(i64)
declare i64 @llvm.haydn.neg64(i64)

define i64 @test_abs64s(i64 %a) {
; CHECK-LABEL: test_abs64s:
; CHECK: abs64s
  %r = call i64 @llvm.haydn.abs64s(i64 %a)
  ret i64 %r
}

define i64 @test_neg64s(i64 %a) {
; CHECK-LABEL: test_neg64s:
; CHECK: neg64s
  %r = call i64 @llvm.haydn.neg64s(i64 %a)
  ret i64 %r
}

define i64 @test_abs64(i64 %a) {
; CHECK-LABEL: test_abs64:
; CHECK: abs64
  %r = call i64 @llvm.haydn.abs64(i64 %a)
  ret i64 %r
}

define i64 @test_neg64(i64 %a) {
; CHECK-LABEL: test_neg64:
; CHECK: neg64
  %r = call i64 @llvm.haydn.neg64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit bitwise: NOT64, SEQ64
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.not64(i64)
declare i64 @llvm.haydn.seq64(i64)

define i64 @test_not64(i64 %a) {
; CHECK-LABEL: test_not64:
; CHECK: not64
  %r = call i64 @llvm.haydn.not64(i64 %a)
  ret i64 %r
}

define i64 @test_seq64(i64 %a) {
; CHECK-LABEL: test_seq64:
; CHECK: seq64
  %r = call i64 @llvm.haydn.seq64(i64 %a)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; 64-bit min/max
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.max64(i64, i64)
declare i64 @llvm.haydn.min64(i64, i64)

define i64 @test_max64(i64 %a, i64 %b) {
; CHECK-LABEL: test_max64:
; CHECK: max64
  %r = call i64 @llvm.haydn.max64(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_min64(i64 %a, i64 %b) {
; CHECK-LABEL: test_min64:
; CHECK: min64
  %r = call i64 @llvm.haydn.min64(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; Dual 32-bit multiply-accumulate/subtract (MULSA32/MULSS32)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.mulsa32.hhll(i64, i64)
declare i64 @llvm.haydn.mulsa32.hllh(i64, i64)
declare i64 @llvm.haydn.mulss32.hhll(i64, i64)
declare i64 @llvm.haydn.mulss32.hllh(i64, i64)

define i64 @test_mulsa32_hhll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulsa32_hhll:
; CHECK: mulsa32_hhll
  %r = call i64 @llvm.haydn.mulsa32.hhll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulsa32_hllh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulsa32_hllh:
; CHECK: mulsa32_hllh
  %r = call i64 @llvm.haydn.mulsa32.hllh(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss32_hhll(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss32_hhll:
; CHECK: mulss32_hhll
  %r = call i64 @llvm.haydn.mulss32.hhll(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @test_mulss32_hllh(i64 %a, i64 %b) {
; CHECK-LABEL: test_mulss32_hllh:
; CHECK: mulss32_hllh
  %r = call i64 @llvm.haydn.mulss32.hllh(i64 %a, i64 %b)
  ret i64 %r
}

;===----------------------------------------------------------------------===;
; DR64 reg shifts (golden: i64 data + i32 amount -> i64)
;===----------------------------------------------------------------------===;

declare i64 @llvm.haydn.sll64(i64, i32)
declare i64 @llvm.haydn.sra64(i64, i32)
declare i64 @llvm.haydn.srl64(i64, i32)
declare i64 @llvm.haydn.sra64r(i64, i32)

define i64 @test_sll64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sll64:
; CHECK: sll64
  %r = call i64 @llvm.haydn.sll64(i64 %a, i32 %b)
  ret i64 %r
}

define i64 @test_sra64(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64:
; CHECK: sra64
  %r = call i64 @llvm.haydn.sra64(i64 %a, i32 %b)
  ret i64 %r
}

define i64 @test_srl64(i64 %a, i32 %b) {
; CHECK-LABEL: test_srl64:
; CHECK: srl64
  %r = call i64 @llvm.haydn.srl64(i64 %a, i32 %b)
  ret i64 %r
}

define i64 @test_sra64r(i64 %a, i32 %b) {
; CHECK-LABEL: test_sra64r:
; CHECK: sra64r
  %r = call i64 @llvm.haydn.sra64r(i64 %a, i32 %b)
  ret i64 %r
}

; (Removed invented satsr64/packsr32 IR — composites live in haydn_dsp.h.)

;===----------------------------------------------------------------------===;
; X2 SIMD non-saturating add/sub
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2add32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32(<2 x i32>, <2 x i32>)

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

;===----------------------------------------------------------------------===;
; X2 SIMD saturating add/sub
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2add32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32s(<2 x i32>, <2 x i32>)

define <2 x i32> @test_x2add32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32s:
; CHECK: x2add32s
  %r = call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sub32s:
; CHECK: x2sub32s
  %r = call <2 x i32> @llvm.haydn.x2sub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X2 SIMD addsub/subadd (dual 32-bit)
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2addsub32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32>, <2 x i32>)

define <2 x i32> @test_x2addsub32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32:
; CHECK: x2addsub32
  %r = call <2 x i32> @llvm.haydn.x2addsub32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2addsub32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32s:
; CHECK: x2addsub32s
  %r = call <2 x i32> @llvm.haydn.x2addsub32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32:
; CHECK: x2subadd32
  %r = call <2 x i32> @llvm.haydn.x2subadd32(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32s(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32s:
; CHECK: x2subadd32s
  %r = call <2 x i32> @llvm.haydn.x2subadd32s(<2 x i32> %a,<2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X4 SIMD non-saturating add/sub (quad 16-bit)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4add16(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16(<4 x i16>, <4 x i16>)

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
; X4 SIMD saturating add/sub (quad 16-bit)
;===----------------------------------------------------------------------===;

declare <4 x i16> @llvm.haydn.x4add16s(<4 x i16>, <4 x i16>)
declare <4 x i16> @llvm.haydn.x4sub16s(<4 x i16>, <4 x i16>)

define <4 x i16> @test_x4add16s(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4add16s:
; CHECK: x4add16s
  %r = call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

define <4 x i16> @test_x4sub16s(<4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: test_x4sub16s:
; CHECK: x4sub16s
  %r = call <4 x i16> @llvm.haydn.x4sub16s(<4 x i16> %a,<4 x i16> %b)
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===;
; X2 HLLH cross-lane variants
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2add32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2add32s.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2sub32s.hllh(<2 x i32>, <2 x i32>)

define <2 x i32> @test_x2add32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32_hllh:
; CHECK: x2add32_hllh
  %r = call <2 x i32> @llvm.haydn.x2add32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2add32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2add32s_hllh:
; CHECK: x2add32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2add32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sub32_hllh:
; CHECK: x2sub32_hllh
  %r = call <2 x i32> @llvm.haydn.x2sub32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2sub32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2sub32s_hllh:
; CHECK: x2sub32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2sub32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

;===----------------------------------------------------------------------===;
; X2 ADDSUB/SUBADD HLLH cross-lane variants
;===----------------------------------------------------------------------===;

declare <2 x i32> @llvm.haydn.x2addsub32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2addsub32s.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32.hllh(<2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2subadd32s.hllh(<2 x i32>, <2 x i32>)

define <2 x i32> @test_x2addsub32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32_hllh:
; CHECK: x2addsub32_hllh
  %r = call <2 x i32> @llvm.haydn.x2addsub32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2addsub32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2addsub32s_hllh:
; CHECK: x2addsub32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2addsub32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32_hllh:
; CHECK: x2subadd32_hllh
  %r = call <2 x i32> @llvm.haydn.x2subadd32.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}

define <2 x i32> @test_x2subadd32s_hllh(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: test_x2subadd32s_hllh:
; CHECK: x2subadd32s_hllh
  %r = call <2 x i32> @llvm.haydn.x2subadd32s.hllh(<2 x i32> %a, <2 x i32> %b)
  ret <2 x i32> %r
}
