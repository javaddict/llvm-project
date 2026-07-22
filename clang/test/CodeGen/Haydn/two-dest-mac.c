// RUN: %clang_cc1 -triple haydn-unknown-elf -O2 -emit-llvm -o - %s | FileCheck %s
//
// -O2 so the haydn_dsp.h helper wrappers (AE_MULP32X2 / __haydn_x2mul32 etc.)
// are inlined into the test functions; the 2-result intrinsic + both
// extractvalues then appear in each define below (at -O0 they stay in the
// out-of-line helper and the per-function CHECK blocks would not see them).
//
// REGRESSION TEST (D400 Path B): the 2-dest SIMD MAC ops lower to 2-result
// LLVM intrinsics, and BOTH results are observable in the IR.
//
// Bug (D400): X2MUL32 / X4MUL16 / X2CMUL32 / X4MULA16S etc. are TRUE 2-OUTPUT
// per the golden (slot1_mac_instruction_list.json: DR_Write_Port:[rtd1,rtd2]),
// but the clang builtin surface modeled them as single-result. The D_RR2/D_RRA2
// _FLEX defs left the rtd2 bits-field orphaned, crashing clang/encoder/verifier.
//
// Fix (D400 Path B): the LLVM intrinsics return `{i64, i64}`. The clang
// builtins use the frexp pattern (`_pair` suffix: returns rtd1, writes rtd2
// through an out-pointer) because the Prototype grammar has no struct return
// and the generic ClangBuiltin<> auto-map cannot lower multi-result intrinsics.
// EmitHaydnBuiltinExpr in Haydn.cpp emits the 2-result intrinsic call +
// CreateExtractValue for each field (mirroring emitFrexpBuiltin).
//
// Test design: each op is exercised via the haydn_dsp.h macro that the
// NatureDSP kernel sources use. We check that the 2-result intrinsic
// (`@llvm.haydn.<op>`) is emitted and that BOTH extractvalue indices (0 and 1)
// appear, proving both halves of the pair survive to IR. If the builtin
// regresses to a single-result call, one of the CHECK lines fails.

#include <haydn_dsp.h>

// CHECK-LABEL: define {{.*}}@test_x2mul32
ae_int32x2 test_x2mul32(ae_int32x2 a, ae_int32x2 b) {
  // X2MUL32 — 2-dest non-accum. AE_MULP32X2 consumes only the low pair.
  // CHECK: call { i64, i64 } @llvm.haydn.x2mul32
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 1
  return AE_MULP32X2(a, b);
}

// CHECK-LABEL: define {{.*}}@test_x4mul16
ae_int64 test_x4mul16(ae_int16x4 d0, ae_int16x4 d1, ae_int16x4 a, ae_int16x4 b) {
  // X4MUL16 — 2-dest non-accum. AE_MUL16X4 writes BOTH d0 (hi pair) and d1 (lo pair).
  // CHECK: call { i64, i64 } @llvm.haydn.x4mul16
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  AE_MUL16X4(d0, d1, a, b);
  return (ae_int64)d0 + (ae_int64)d1;
}

// CHECK-LABEL: define {{.*}}@test_x4mula16s
ae_int64 test_x4mula16s(ae_int64 acc_hi, ae_int64 acc_lo,
                        ae_int16x4 a, ae_int16x4 b) {
  // X4MULA16S — 2-dest accumulator. AE_MULAF16X4SS updates BOTH accumulators.
  // CHECK: call { i64, i64 } @llvm.haydn.x4mula16s
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  AE_MULAF16X4SS(acc_hi, acc_lo, a, b);
  return acc_hi + acc_lo;
}

// CHECK-LABEL: define {{.*}}@test_x2cmul32
ae_int32x2 test_x2cmul32(ae_int32x2 a, ae_int32x2 b) {
  // X2CMUL32 — 2-dest complex multiply. AE_MULC32X16_H selects the real (hi) half.
  // CHECK: call { i64, i64 } @llvm.haydn.x2cmul32
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 0
  return AE_MULC32X16_H(a, b);
}

// CHECK-LABEL: define {{.*}}@test_x2cmul32_lo
ae_int32x2 test_x2cmul32_lo(ae_int32x2 a, ae_int32x2 b) {
  // AE_MULC32X16_L selects the imag (lo) half — extractvalue index 1.
  // CHECK: call { i64, i64 } @llvm.haydn.x2cmul32
  // CHECK: extractvalue { i64, i64 } %{{.*}}, 1
  return AE_MULC32X16_L(a, b);
}

// CHECK-LABEL: define {{.*}}@test_x2mula32
ae_int64 test_x2mula32(ae_int64 acc_hi, ae_int64 acc_lo,
                       ae_int32x2 a, ae_int32x2 b) {
  // X2MULA32 — 2-dest accumulator. AE_MULA32X2 4-arg form updates BOTH accs.
  // CHECK: call { i64, i64 } @llvm.haydn.x2mula32
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  AE_MULA32X2(acc_hi, acc_lo, a, b);
  return acc_hi + acc_lo;
}

// CHECK-LABEL: define {{.*}}@test_x4muls16s
ae_int64 test_x4muls16s(ae_int64 acc_hi, ae_int64 acc_lo,
                        ae_int16x4 a, ae_int16x4 b) {
  // X4MULS16S — 2-dest subtract accumulator.
  // CHECK: call { i64, i64 } @llvm.haydn.x4muls16s
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 0
  // CHECK-DAG: extractvalue { i64, i64 } %{{.*}}, 1
  AE_MULSF16X4SS(acc_hi, acc_lo, a, b);
  return acc_hi + acc_lo;
}
