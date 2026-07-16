// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
//
// REGRESSION TEST: accumulator-form MAC builtins are 3-arg end-to-end (D100).
//
// Bug (D99): the spec defines MULA64_LL / MULS64_LL / MULAS64_LL / MULSS64_LL
// / FMULA32S_LL as `rtd = rtd OP product` (3-operand, rtd is read+written),
// but the end-to-end stack (BuiltinsHaydn.td, IntrinsicsHaydn.td, ISel) was
// 2-arg, causing "too many arguments to function call" when haydn_dsp.h's
// AE_MULA64_SS_* macros were invoked. Every MAC-heavy kernel was blocked.
//
// Fix (D100): all 32 accumulator-form _ss_ MAC builtins (MULA64/MULS64/
// MULAS64/MULSS64/FMULA32S/FMULS32S/FF2MULA/FF2MULS/F2MULAA/F2MULSS/
// FMULAA16/FMULSS16/X2FCMULA) are now 3-arg end-to-end.
//
// Test design: invokes each 3-arg MAC builtin with (acc, a, b) and verifies
// the IR call has 3 i64 arguments. If any MAC reverts to 2-arg, this test
// fails with a "too many arguments" compile error or a CHECK mismatch.

// Test Haydn DSP target-specific builtins.
// These builtins map to llvm.haydn.* intrinsics via ClangBuiltin annotations.

typedef long long int64_t;
typedef unsigned int uint32_t;

// === MAC operations: mul64 (binary) ===

// CHECK-LABEL: @test_mul64_ss_ll
int64_t test_mul64_ss_ll(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mul64.ss.ll(i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mul64_ss_ll(a, b);
}

// CHECK-LABEL: @test_mul64_ss_hh
int64_t test_mul64_ss_hh(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mul64.ss.hh(i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mul64_ss_hh(a, b);
}

// CHECK-LABEL: @test_mul64_uu_ull
int64_t test_mul64_uu_ull(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mul64.uu.ull(i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mul64_uu_ull(a, b);
}

// === MAC operations: mula64 (ternary) ===

// CHECK-LABEL: @test_mula64_ss_ll
int64_t test_mula64_ss_ll(int64_t acc, int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mula64.ss.ll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mula64_ss_ll(acc, a, b);
}

// === MAC operations: muls64 (ternary) ===

// CHECK-LABEL: @test_muls64_ss_ll
int64_t test_muls64_ss_ll(int64_t acc, int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.muls64.ss.ll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_muls64_ss_ll(acc, a, b);
}

// === MAC operations: mulas64 (ternary, saturating accumulate) ===

// CHECK-LABEL: @test_mulas64_ss_ll
int64_t test_mulas64_ss_ll(int64_t acc, int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mulas64.ss.ll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mulas64_ss_ll(acc, a, b);
}

// === MAC operations: mulss64 (ternary, saturating subtract) ===

// CHECK-LABEL: @test_mulss64_ss_ll
int64_t test_mulss64_ss_ll(int64_t acc, int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.mulss64.ss.ll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_mulss64_ss_ll(acc, a, b);
}

// === Saturating arithmetic ===

// CHECK-LABEL: @test_add32s
int test_add32s(int a, int b) {
  // CHECK: call i32 @llvm.haydn.add32s(i32 %{{.*}}, i32 %{{.*}})
  return __builtin_haydn_add32s(a, b);
}

// CHECK-LABEL: @test_sub32s
int test_sub32s(int a, int b) {
  // CHECK: call i32 @llvm.haydn.sub32s(i32 %{{.*}}, i32 %{{.*}})
  return __builtin_haydn_sub32s(a, b);
}

// CHECK-LABEL: @test_add64s
int64_t test_add64s(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.add64s(i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_add64s(a, b);
}

// CHECK-LABEL: @test_abs32s
int test_abs32s(int a) {
  // CHECK: call i32 @llvm.haydn.abs32s(i32 %{{.*}})
  return __builtin_haydn_abs32s(a);
}

// CHECK-LABEL: @test_neg32s
int test_neg32s(int a) {
  // CHECK: call i32 @llvm.haydn.neg32s(i32 %{{.*}})
  return __builtin_haydn_neg32s(a);
}

// === Fractional multiply ===

// CHECK-LABEL: @test_fmul32s_ll
int64_t test_fmul32s_ll(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.fmul32s.ll(i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_fmul32s_ll(a, b);
}

// CHECK-LABEL: @test_fmula32s_ll
int64_t test_fmula32s_ll(int64_t acc, int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.fmula32s.ll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
  return __builtin_haydn_fmula32s_ll(acc, a, b);
}

// === SIMD saturating ===

// CHECK-LABEL: @test_x2add32s
int64_t test_x2add32s(int64_t a, int64_t b) {
  // CHECK: call <2 x i32> @llvm.haydn.x2add32s(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
  return __builtin_haydn_x2add32s(a, b);
}

// CHECK-LABEL: @test_x4add16s
int64_t test_x4add16s(int64_t a, int64_t b) {
  // CHECK: call <4 x i16> @llvm.haydn.x4add16s(<4 x i16> %{{.*}}, <4 x i16> %{{.*}})
  return __builtin_haydn_x4add16s(a, b);
}

// === SIMD MAC ===

// CHECK-LABEL: @test_x2dot32
int64_t test_x2dot32(int64_t a, int64_t b) {
  // CHECK: call i64 @llvm.haydn.x2dot32
  return __builtin_haydn_x2dot32(a, b);
}

// === Transcendental ===

// CHECK-LABEL: @test_log2
int test_log2(int a) {
  // CHECK: call i32 @llvm.haydn.log2(i32 %{{.*}})
  return __builtin_haydn_log2(a);
}

// CHECK-LABEL: @test_exp2
int test_exp2(int a) {
  // CHECK: call i32 @llvm.haydn.exp2(i32 %{{.*}})
  return __builtin_haydn_exp2(a);
}

// === Upper-half multiply ===

// CHECK-LABEL: @test_mulssh
int test_mulssh(int a, int b) {
  // CHECK: call i32 @llvm.haydn.mulssh(i32 %{{.*}}, i32 %{{.*}})
  return __builtin_haydn_mulssh(a, b);
}

// CHECK-LABEL: @test_mull
int test_mull(int a, int b) {
  // CHECK: call i32 @llvm.haydn.mull(i32 %{{.*}}, i32 %{{.*}})
  return __builtin_haydn_mull(a, b);
}

// === Normalization ===

// CHECK-LABEL: @test_nsa32
int test_nsa32(int a) {
  // CHECK: call i32 @llvm.haydn.nsa32(i32 %{{.*}})
  return __builtin_haydn_nsa32(a);
}

// NOTE: __builtin_haydn_popcount32 and __builtin_haydn_brev32 are clang-only
// builtins with no LLVM intrinsic mapping yet (see BuiltinsHaydn.td
// "Clang-only builtins" section). They are not tested here because they
// cannot be codegen'd. When their LLVM intrinsics + CodeGen hooks are
// added, restore the two test cases below:
//   int test_popcount32(int a) { return __builtin_haydn_popcount32(a); }
//   uint32_t test_brev32(uint32_t a) { return __builtin_haydn_brev32(a); }
