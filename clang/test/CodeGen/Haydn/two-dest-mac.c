// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-llvm -o - %s | FileCheck %s
//
// REGRESSION TEST (D400 Path B): frexp-pattern _pair builtins (generated
// haydn_builtin_cg.inc) lower to 2-result LLVM intrinsics with both
// extractvalue indices observable.
//
// Golden lanes: X2 sources <2 x i32>, X4 sources <4 x i16>; accs stay i64.

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));

// CHECK-LABEL: @test_x2mul32
int64_t test_x2mul32(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32>
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x2mul32_pair(&lo, a, b);
}

// CHECK-LABEL: @test_x4mul16
int64_t test_x4mul16(haydn_x4int16 a, haydn_x4int16 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x4mul16(<4 x i16>
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x4mul16_pair(&lo, a, b);
}

// CHECK-LABEL: @test_x4mula16s
int64_t test_x4mula16s(int64_t acc_hi, int64_t acc_lo, haydn_x4int16 a,
                       haydn_x4int16 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x4mula16s(i64
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x4mula16s_pair(&lo, acc_hi, acc_lo, a, b);
}

// CHECK-LABEL: @test_x2cmul32
int64_t test_x2cmul32(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x2cmul32(<2 x i32>
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x2cmul32_pair(&lo, a, b);
}

// CHECK-LABEL: @test_x2mula32
int64_t test_x2mula32(int64_t acc_hi, int64_t acc_lo, haydn_x2int32 a,
                      haydn_x2int32 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x2mula32(i64
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x2mula32_pair(&lo, acc_hi, acc_lo, a, b);
}

// CHECK-LABEL: @test_x4muls16s
int64_t test_x4muls16s(int64_t acc_hi, int64_t acc_lo, haydn_x4int16 a,
                       haydn_x4int16 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x4muls16s(i64
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  return __builtin_haydn_x4muls16s_pair(&lo, acc_hi, acc_lo, a, b);
}
