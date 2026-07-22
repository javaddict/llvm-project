// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
//
// Golden-lane type truth: X2 parallel 32-bit ops lower as <2 x i32>,
// X4 parallel 16-bit ops as <4 x i16>. Reduce ops take vector srcs and
// return i64. Pair frexp MAC takes vector srcs + i64 accumulators.

typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));
typedef long long int64_t;

// --- same-width SIMD ---

// CHECK-LABEL: @test_x2add32
// CHECK: call <2 x i32> @llvm.haydn.x2add32(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
haydn_x2int32 test_x2add32(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_x2add32(a, b);
}

// CHECK-LABEL: @test_x2abs32
// CHECK: call <2 x i32> @llvm.haydn.x2abs32(<2 x i32> %{{.*}})
haydn_x2int32 test_x2abs32(haydn_x2int32 a) {
  return __builtin_haydn_x2abs32(a);
}

// CHECK-LABEL: @test_x4add16
// CHECK: call <4 x i16> @llvm.haydn.x4add16(<4 x i16> %{{.*}}, <4 x i16> %{{.*}})
haydn_x4int16 test_x4add16(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4add16(a, b);
}

// CHECK-LABEL: @test_x4fcmul16rs
// CHECK: call <4 x i16> @llvm.haydn.x4fcmul16rs(<4 x i16> %{{.*}}, <4 x i16> %{{.*}})
haydn_x4int16 test_x4fcmul16rs(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4fcmul16rs(a, b);
}

// CHECK-LABEL: @test_x4fcmula16rs
// CHECK: call <4 x i16> @llvm.haydn.x4fcmula16rs(<4 x i16> %{{.*}}, <4 x i16> %{{.*}}, <4 x i16> %{{.*}})
haydn_x4int16 test_x4fcmula16rs(haydn_x4int16 acc, haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4fcmula16rs(acc, a, b);
}

// CHECK-LABEL: @test_x2slli32
// CHECK: call <2 x i32> @llvm.haydn.x2slli32(<2 x i32> %{{.*}}, i32 3)
haydn_x2int32 test_x2slli32(haydn_x2int32 a) {
  return __builtin_haydn_x2slli32(a, 3);
}

// CHECK-LABEL: @test_maxabs32s
// CHECK: call <2 x i32> @llvm.haydn.maxabs32s(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
haydn_x2int32 test_maxabs32s(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_maxabs32s(a, b);
}

// Mixed: four 32-bit inputs → four saturated 16-bit lanes.
// CHECK-LABEL: @test_x4sat32t16
// CHECK: call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
haydn_x4int16 test_x4sat32t16(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_x4sat32t16(a, b);
}

// --- reduce: vector in, i64 out ---

// CHECK-LABEL: @test_x4energy16
// CHECK: call i64 @llvm.haydn.x4energy16(<4 x i16> %{{.*}})
int64_t test_x4energy16(haydn_x4int16 a) {
  return __builtin_haydn_x4energy16(a);
}

// CHECK-LABEL: @test_x2dot32
// CHECK: call i64 @llvm.haydn.x2dot32(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
int64_t test_x2dot32(haydn_x2int32 a, haydn_x2int32 b) {
  return __builtin_haydn_x2dot32(a, b);
}

// CHECK-LABEL: @test_x2hadd32_l
// CHECK: call i64 @llvm.haydn.x2hadd32.l(<2 x i32> %{{.*}})
int64_t test_x2hadd32_l(haydn_x2int32 a) {
  return __builtin_haydn_x2hadd32_l(a);
}

// --- pair frexp: vector sources, i64 products ---

// CHECK-LABEL: @test_x2mul32_pair
// CHECK: call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
int64_t test_x2mul32_pair(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  return __builtin_haydn_x2mul32_pair(&lo, a, b);
}

// CHECK-LABEL: @test_x2mula32_pair
// CHECK: call { i64, i64 } @llvm.haydn.x2mula32(i64 %{{.*}}, i64 %{{.*}}, <2 x i32> %{{.*}}, <2 x i32> %{{.*}})
int64_t test_x2mula32_pair(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                           haydn_x2int32 b) {
  int64_t lo;
  return __builtin_haydn_x2mula32_pair(&lo, acc1, acc2, a, b);
}

// CHECK-LABEL: @test_x4mula16_pair
// CHECK: call { i64, i64 } @llvm.haydn.x4mula16(i64 %{{.*}}, i64 %{{.*}}, <4 x i16> %{{.*}}, <4 x i16> %{{.*}})
int64_t test_x4mula16_pair(int64_t acc1, int64_t acc2, haydn_x4int16 a,
                           haydn_x4int16 b) {
  int64_t lo;
  return __builtin_haydn_x4mula16_pair(&lo, acc1, acc2, a, b);
}
