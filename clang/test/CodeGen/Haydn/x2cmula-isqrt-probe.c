// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-llvm -O0 -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-llvm -O2 -o - %s | FileCheck %s --check-prefixes=CHECK,CHECK-O2
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-obj -O0 -o %t.o0.o %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -emit-obj -O2 -o %t.o2.o %s
// REQUIRES: haydn-registered-target
//
// C0.3 G-CAPI closure: x2cmula32{,s}/x2cmuls32{,s} are frexp haydn_dpair_t
// pairs composed over llvm.haydn.x2cmul32{s} + add64/sub64(+sat). isqrt is
// SoftISqrt pure-ALU. No phantom llvm.haydn.x2cmula* / isqrt intrinsics.
// Full C→object at -O0 and -O2 (emit-obj RUN lines above).
//
// Return hi^lo so both frexp halves stay live under -O2 DCE.

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));

// CHECK-LABEL: @test_x2cmula32
// CHECK: call {{.*}}@llvm.haydn.x2cmul32(
// CHECK-DAG: call {{.*}}@llvm.haydn.add64(
// CHECK-DAG: call {{.*}}@llvm.haydn.add64(
// CHECK-NOT: llvm.haydn.x2cmula32
int64_t test_x2cmula32(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                       haydn_x2int32 b) {
  int64_t lo;
  int64_t hi = __builtin_haydn_x2cmula32_pair(&lo, acc1, acc2, a, b);
  return hi ^ lo;
}

// CHECK-LABEL: @test_x2cmula32s
// CHECK: call {{.*}}@llvm.haydn.x2cmul32s(
// CHECK-DAG: call {{.*}}@llvm.haydn.add64s(
// CHECK-DAG: call {{.*}}@llvm.haydn.add64s(
// CHECK-NOT: llvm.haydn.x2cmula32s
int64_t test_x2cmula32s(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                        haydn_x2int32 b) {
  int64_t lo;
  int64_t hi = __builtin_haydn_x2cmula32s_pair(&lo, acc1, acc2, a, b);
  return hi ^ lo;
}

// CHECK-LABEL: @test_x2cmuls32
// CHECK: call {{.*}}@llvm.haydn.x2cmul32(
// CHECK-DAG: call {{.*}}@llvm.haydn.sub64(
// CHECK-DAG: call {{.*}}@llvm.haydn.sub64(
// CHECK-NOT: @llvm.haydn.x2cmuls32
int64_t test_x2cmuls32(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                       haydn_x2int32 b) {
  int64_t lo;
  int64_t hi = __builtin_haydn_x2cmuls32_pair(&lo, acc1, acc2, a, b);
  return hi ^ lo;
}

// CHECK-LABEL: @test_x2cmuls32s
// CHECK: call {{.*}}@llvm.haydn.x2cmul32s(
// CHECK-DAG: call {{.*}}@llvm.haydn.sub64s(
// CHECK-DAG: call {{.*}}@llvm.haydn.sub64s(
// CHECK-NOT: @llvm.haydn.x2cmuls32s
int64_t test_x2cmuls32s(int64_t acc1, int64_t acc2, haydn_x2int32 a,
                        haydn_x2int32 b) {
  int64_t lo;
  int64_t hi = __builtin_haydn_x2cmuls32s_pair(&lo, acc1, acc2, a, b);
  return hi ^ lo;
}

// SoftISqrt: pure ALU — no llvm.haydn.isqrt. O0 keeps icmp/lshr expand;
// O2 may fold the clamp (smax) but still has no haydn isqrt intrinsic.
// CHECK-LABEL: @test_isqrt
// CHECK-NOT: llvm.haydn.isqrt
// CHECK-O0: icmp slt
// CHECK-O0: lshr
// CHECK-O2: lshr
int test_isqrt(int a) {
  return __builtin_haydn_isqrt(a);
}
