// RUN: %clang_cc1 -triple haydn-unknown-elf -emit-llvm -o - %s | FileCheck %s
//
// CodeGen for frexp-pattern _pair builtins (haydn_builtin_cg.inc).
// Golden lanes: X2 sources are <2 x i32>, X4 sources are <4 x i16>;
// products/accs stay i64. Switch arms emit {i64,i64} + extractvalue.
// CB load remains {i64,i32}.

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));

// CHECK-LABEL: @test_x2mul32_pair
int64_t test_x2mul32_pair(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x2mul32(<2 x i32> %{{.*}}, <2 x i32> %{{.*}})
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 1
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 0
  return __builtin_haydn_x2mul32_pair(&lo, a, b);
}

// CHECK-LABEL: @test_x2mula32_pair
int64_t test_x2mula32_pair(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                           haydn_x2int32 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x2mula32(i64 %{{.*}}, i64 %{{.*}}, <2 x i32> %{{.*}}, <2 x i32> %{{.*}})
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 1
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 0
  return __builtin_haydn_x2mula32_pair(&lo, acc1, acc2, a, b);
}

// CHECK-LABEL: @test_x4mul16_pair
int64_t test_x4mul16_pair(haydn_x4int16 a, haydn_x4int16 b) {
  int64_t lo;
  // CHECK: call { i64, i64 } @llvm.haydn.x4mul16(<4 x i16> %{{.*}}, <4 x i16> %{{.*}})
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 1
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 0
  return __builtin_haydn_x4mul16_pair(&lo, a, b);
}

// CHECK-LABEL: @test_ldw_cb_imm_pair
int64_t test_ldw_cb_imm_pair(int base, int cbr, int stride) {
  int np;
  // CHECK: call { i64, i32 } @llvm.haydn.ldw.cb.imm
  // CHECK: extractvalue { i64, i32 } %{{.*}}, 1
  // CHECK: extractvalue { i64, i32 } %{{.*}}, 0
  return __builtin_haydn_ldw_cb_imm_pair(&np, base, cbr, stride);
}
