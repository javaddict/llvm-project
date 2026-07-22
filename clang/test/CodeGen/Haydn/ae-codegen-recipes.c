// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
//
// AE CodeGen= recipes from BuiltinsHaydn.td → haydn_builtin_cg.inc
// (no hand-rolled switch arms left for these in TargetBuiltins/Haydn.cpp).

typedef long long int64_t;

// CHECK-LABEL: @test_mul16js
int64_t test_mul16js(int64_t a) {
  // CHECK: call <4 x i16> @llvm.haydn.x4mjswap16s
  return __builtin_ae_mul16js(a);
}

// CHECK-LABEL: @test_conj16s
int64_t test_conj16s(int64_t a) {
  // CHECK: call <4 x i16> @llvm.haydn.x4conj16s
  return __builtin_ae_conj16s(a);
}

// CHECK-LABEL: @test_mulafc16ras
int64_t test_mulafc16ras(int64_t acc, int64_t a, int64_t b) {
  // Golden lanes: X4FCMULA16RS is <4 x i16>; AE bag surface bitcasts.
  // CHECK: call <4 x i16> @llvm.haydn.x4fcmula16rs
  return __builtin_ae_mulafc16ras(acc, a, b);
}

// CHECK-LABEL: @test_addandsubrng
int64_t test_addandsubrng(int64_t a, int64_t b) {
  // CHECK: call <4 x i16> @llvm.haydn.x4add16s
  // CHECK: call <4 x i16> @llvm.haydn.x4sub16s
  // CHECK: call void @llvm.haydn.movegpr2sfr
  // CHECK: call <4 x i16> @llvm.haydn.x4movt16
  return __builtin_ae_addandsubrng16ras_s0(a, b);
}
