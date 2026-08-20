// RUN: python3 %S/s2-clang-llvm-signature-parity.py \
// RUN:   %S/../../../include/clang/Basic/BuiltinsHaydn.td \
// RUN:   %S/../../../../llvm/include/llvm/IR/IntrinsicsHaydn.td
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding \
// RUN:   -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// Mechanical Clang↔LLVM name/arity parity plus a live ternary FMULAS32S
// lowering pin. A five-argument Clang builtin against a ternary LLVM
// intrinsic must fail the python gate.

// CHECK-LABEL: define {{.*}} @test_fmulas32s_hhll
// CHECK: call i64 @llvm.haydn.fmulas32s.hhll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
long long test_fmulas32s_hhll(long long acc, long long a, long long b) {
  return __builtin_haydn_fmulas32s_hhll(acc, a, b);
}

// CHECK-LABEL: define {{.*}} @test_fmulsa32s_hllh
// CHECK: call i64 @llvm.haydn.fmulsa32s.hllh(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
long long test_fmulsa32s_hllh(long long acc, long long a, long long b) {
  return __builtin_haydn_fmulsa32s_hllh(acc, a, b);
}

// CHECK-LABEL: define {{.*}} @test_smula16_00
// CHECK: call i64 @llvm.haydn.smula16.00(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
long long test_smula16_00(long long acc, long long a, long long b) {
  return __builtin_haydn_smula16_00(acc, a, b);
}

// CHECK-LABEL: define {{.*}} @test_mulsa32_hhll
// CHECK: call i64 @llvm.haydn.mulsa32.hhll(i64 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
long long test_mulsa32_hhll(long long acc, long long a, long long b) {
  return __builtin_haydn_mulsa32_hhll(acc, a, b);
}
