; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -o - %t/supported.ll | FileCheck %s --check-prefix=OK
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/fpclass_vec.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=VEC
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o /dev/null %t/f128_fpext.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=F128

; Role: semantic — closed legalizer↔RTLIB contract for soft-float libm/compiler-rt.
;
; REGRESSION TEST: 13 FP ops were shipping ICEs; __fp16 FPEXT clamped to
; s32←s32; G_IS_FPCLASS leftover widths llvm_unreachable.
;
; Bug: HaydnLegalizerInfo advertised .libcallFor for frem/sqrt/fma/sin/cos/
; exp/log/log2/log10/pow/powi and .lowerFor for trunc/round/roundeven, but
; HaydnSubtarget::initLibcallLoweringInfo did not register RTLIB impl names
; (and G_INTRINSIC_TRUNC has no generic lower). llc aborted:
;   LLVM ERROR: unable to legalize instruction: G_FREM / G_FSQRT / …
; Half load/fpext clamped s16→s32 before the libcall check, producing
; unlegalizable G_FPEXT s32←s32. G_IS_FPCLASS used a bare .lower() that
; called getFltSemanticForLLT on non-IEEE widths (assert/unreachable).
;
; Fix: one libcall-name table in HaydnSubtarget::initLibcallLoweringInfo
; covering every advertised .libcallFor; trunc/round/roundeven moved to
; libcallFor; f16 converts custom-libcall via integer-bit GPR ABI;
; G_IS_FPCLASS lowerFor {{s1,s32},{s1,s64}} then unsupported.
;
; Test design: each former ICE op is a named function whose CHECK pins the
; registered symbol. If a name is dropped, llc ICEs or the jal_w CHECK
; fails. Half functions must compile (no s32←s32 abort). is.fpclass must
; lower to integer bit tests, not a libcall and not a crash. Vector
; is.fpclass and fp128 fpext must diagnose, not assert.
;
; If this file is "fixed" by relaxing CHECKs to accept an abort, the 13
; ICEs have returned.

;--- supported.ll

define float @frem_f32(float %a, float %b) {
; OK-LABEL: frem_f32:
; OK: lui{{.*}}fmodf
; OK: addi32{{.*}}fmodf
; OK: jalr{{.*}}lr
  %r = frem float %a, %b
  ret float %r
}

define double @frem_f64(double %a, double %b) {
; OK-LABEL: frem_f64:
; OK: lui{{.*}}fmod
; OK: addi32{{.*}}fmod
; OK: jalr{{.*}}lr
  %r = frem double %a, %b
  ret double %r
}

define float @sqrt_f32(float %a) {
; OK-LABEL: sqrt_f32:
; OK: lui{{.*}}sqrtf
; OK: addi32{{.*}}sqrtf
; OK: jalr{{.*}}lr
  %r = call float @llvm.sqrt.f32(float %a)
  ret float %r
}

define float @fma_f32(float %a, float %b, float %c) {
; OK-LABEL: fma_f32:
; OK: lui{{.*}}fmaf
; OK: addi32{{.*}}fmaf
; OK: jalr{{.*}}lr
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

define float @sin_f32(float %a) {
; OK-LABEL: sin_f32:
; OK: lui{{.*}}sinf
; OK: addi32{{.*}}sinf
; OK: jalr{{.*}}lr
  %r = call float @llvm.sin.f32(float %a)
  ret float %r
}

define float @cos_f32(float %a) {
; OK-LABEL: cos_f32:
; OK: lui{{.*}}cosf
; OK: addi32{{.*}}cosf
; OK: jalr{{.*}}lr
  %r = call float @llvm.cos.f32(float %a)
  ret float %r
}

define float @exp_f32(float %a) {
; OK-LABEL: exp_f32:
; OK: lui{{.*}}expf
; OK: addi32{{.*}}expf
; OK: jalr{{.*}}lr
  %r = call float @llvm.exp.f32(float %a)
  ret float %r
}

define float @log_f32(float %a) {
; OK-LABEL: log_f32:
; OK: lui{{.*}}logf
; OK: addi32{{.*}}logf
; OK: jalr{{.*}}lr
  %r = call float @llvm.log.f32(float %a)
  ret float %r
}

define float @log2_f32(float %a) {
; OK-LABEL: log2_f32:
; OK: lui{{.*}}log2f
; OK: addi32{{.*}}log2f
; OK: jalr{{.*}}lr
  %r = call float @llvm.log2.f32(float %a)
  ret float %r
}

define float @log10_f32(float %a) {
; OK-LABEL: log10_f32:
; OK: lui{{.*}}log10f
; OK: addi32{{.*}}log10f
; OK: jalr{{.*}}lr
  %r = call float @llvm.log10.f32(float %a)
  ret float %r
}

define float @pow_f32(float %a, float %b) {
; OK-LABEL: pow_f32:
; OK: lui{{.*}}powf
; OK: addi32{{.*}}powf
; OK: jalr{{.*}}lr
  %r = call float @llvm.pow.f32(float %a, float %b)
  ret float %r
}

define double @pow_f64(double %a, double %b) {
; OK-LABEL: pow_f64:
; OK: lui{{.*}}pow
; OK: addi32{{.*}}pow
; OK: jalr{{.*}}lr
  %r = call double @llvm.pow.f64(double %a, double %b)
  ret double %r
}

define float @powi_f32(float %a, i32 %n) {
; OK-LABEL: powi_f32:
; OK: lui{{.*}}__powisf2
; OK: addi32{{.*}}__powisf2
; OK: jalr{{.*}}lr
  %r = call float @llvm.powi.f32.i32(float %a, i32 %n)
  ret float %r
}

define float @trunc_f32(float %a) {
; OK-LABEL: trunc_f32:
; OK: lui{{.*}}truncf
; OK: addi32{{.*}}truncf
; OK: jalr{{.*}}lr
  %r = call float @llvm.trunc.f32(float %a)
  ret float %r
}

define float @round_f32(float %a) {
; OK-LABEL: round_f32:
; OK: lui{{.*}}roundf
; OK: addi32{{.*}}roundf
; OK: jalr{{.*}}lr
  %r = call float @llvm.round.f32(float %a)
  ret float %r
}

define float @roundeven_f32(float %a) {
; OK-LABEL: roundeven_f32:
; OK: lui{{.*}}roundevenf
; OK: addi32{{.*}}roundevenf
; OK: jalr{{.*}}lr
  %r = call float @llvm.roundeven.f32(float %a)
  ret float %r
}

define float @fp16_load_fpext(ptr %p) {
; OK-LABEL: fp16_load_fpext:
; OK: lui{{.*}}__extendhfsf2
; OK: addi32{{.*}}__extendhfsf2
; OK: jalr{{.*}}lr
  %h = load half, ptr %p, align 2
  %f = fpext half %h to float
  ret float %f
}

define void @fp16_fptrunc_store(ptr %p, float %f) {
; OK-LABEL: fp16_fptrunc_store:
; OK: lui{{.*}}__truncsfhf2
; OK: addi32{{.*}}__truncsfhf2
; OK: jalr{{.*}}lr
  %h = fptrunc float %f to half
  store half %h, ptr %p, align 2
  ret void
}

define double @fp16_load_fpext_f64(ptr %p) {
; OK-LABEL: fp16_load_fpext_f64:
; OK: lui{{.*}}__extendhfdf2
; OK: addi32{{.*}}__extendhfdf2
; OK: jalr{{.*}}lr
  %h = load half, ptr %p, align 2
  %d = fpext half %h to double
  ret double %d
}

define i1 @fpclass_nan_f32(float %a) {
; OK-LABEL: fpclass_nan_f32:
; OK-NOT: jal_w{{(\.s[012])?}}	lr, isnan
; OK: jalr
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 3)
  ret i1 %r
}

define i1 @fpclass_nan_f64(double %a) {
; OK-LABEL: fpclass_nan_f64:
; OK-NOT: jal_w{{(\.s[012])?}}	lr, isnan
; OK: jalr
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 3)
  ret i1 %r
}

define i1 @fpclass_inf_f32(float %a) {
; OK-LABEL: fpclass_inf_f32:
; OK-NOT: jal_w{{(\.s[012])?}}	lr, isinf
; OK: jalr
  %r = call i1 @llvm.is.fpclass.f32(float %a, i32 516)
  ret i1 %r
}

define i1 @fpclass_finite_f64(double %a) {
; OK-LABEL: fpclass_finite_f64:
; OK-NOT: jal_w{{(\.s[012])?}}	lr, isfinite
; OK: jalr
  %r = call i1 @llvm.is.fpclass.f64(double %a, i32 504)
  ret i1 %r
}

declare float @llvm.sqrt.f32(float)
declare float @llvm.fma.f32(float, float, float)
declare float @llvm.sin.f32(float)
declare float @llvm.cos.f32(float)
declare float @llvm.exp.f32(float)
declare float @llvm.log.f32(float)
declare float @llvm.log2.f32(float)
declare float @llvm.log10.f32(float)
declare float @llvm.pow.f32(float, float)
declare double @llvm.pow.f64(double, double)
declare float @llvm.powi.f32.i32(float, i32)
declare float @llvm.trunc.f32(float)
declare float @llvm.round.f32(float)
declare float @llvm.roundeven.f32(float)
declare i1 @llvm.is.fpclass.f32(float, i32)
declare i1 @llvm.is.fpclass.f64(double, i32)

;--- fpclass_vec.ll

define <2 x i1> @fpclass_v2f32(<2 x float> %a) {
  %r = call <2 x i1> @llvm.is.fpclass.v2f32(<2 x float> %a, i32 3)
  ret <2 x i1> %r
}
declare <2 x i1> @llvm.is.fpclass.v2f32(<2 x float>, i32)
; VEC: {{unable to legalize instruction: .*G_IS_FPCLASS|LLVM ERROR: .*G_IS_FPCLASS}}

;--- f128_fpext.ll

define fp128 @f128_fpext(float %a) {
  %r = fpext float %a to fp128
  ret fp128 %r
}
; Fail-closed: leftover f128 convert is unsupported. Return/store of s128
; may be diagnosed first (same class as language-coverage-matrix.ll).
; F128: {{unable to legalize instruction: .*G_(FPEXT|FPTRUNC|STORE|LOAD)}}
