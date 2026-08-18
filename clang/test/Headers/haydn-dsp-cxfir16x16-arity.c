// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s
//
// REQUIRES: haydn-registered-target
//
// NatureDSP cxfir16x16_hifi3 (non-quad path) uses:
//   q = AE_MULZASFD32X16_H3_L2(d, c);     // 2-arg assignment
//   q = AE_MULZAAFD32X16_H2_L3(d, c);     // 2-arg assignment
//   AE_MULASFD32X16_H1_L0(q, d, c);       // 3-arg statement write-back
//   AE_MULASFD32X16_H3_L2(q, d, c);
//   AE_MULAAFD32X16_H0_L1(q, d, c);
//   AE_MULAAFD32X16_H2_L3(q, d, c);
// A last-site 3-arg-only MULZAA H2_L3 write-back rejected the 2-arg form
// ("too few arguments provided to function-like macro invocation").
// Statement-form MULAS must write back (return of f2mulss must not be
// discarded). Add-add maps to f2mulaa32rs_hhll; sub-sub to f2mulss32rs_hhll.

#include <haydn_dsp.h>

#if defined(AE_MULAAAAQ16)
_Static_assert(0, "AE_MULAAAAQ16 must be undefined under default strict");
#endif

// CHECK-LABEL: @zaa_h2_l3_2arg
// CHECK: call {{.*}}@llvm.haydn.f2mulaa32rs.hhll
// CHECK-NOT: mulafd32x16x2.fir
ae_int64 zaa_h2_l3_2arg(ae_int16x4 d, ae_int16x4 c) {
  return AE_MULZAAFD32X16_H2_L3(d, c);
}

// CHECK-LABEL: @zaa_h2_l3_3arg
// CHECK: call {{.*}}@llvm.haydn.f2mulaa32rs.hhll
ae_int64 zaa_h2_l3_3arg(ae_int64 acc, ae_int16x4 d, ae_int16x4 c) {
  AE_MULZAAFD32X16_H2_L3(acc, d, c);
  return acc;
}

// CHECK-LABEL: @zas_h3_l2_2arg
// CHECK: call {{.*}}@llvm.haydn.f2mulss32rs.hhll
// CHECK-NOT: mulafd32x16x2.fir
ae_int64 zas_h3_l2_2arg(ae_int16x4 d, ae_int16x4 c) {
  return AE_MULZASFD32X16_H3_L2(d, c);
}

// CHECK-LABEL: @as_h1_l0_stmt
// CHECK: call {{.*}}@llvm.haydn.f2mulss32rs.hhll
ae_int64 as_h1_l0_stmt(ae_int64 acc, ae_int16x4 d, ae_int16x4 c) {
  AE_MULASFD32X16_H1_L0(acc, d, c);
  return acc;
}

// CHECK-LABEL: @as_h3_l2_stmt
// CHECK: call {{.*}}@llvm.haydn.f2mulss32rs.hhll
ae_int64 as_h3_l2_stmt(ae_int64 acc, ae_int16x4 d, ae_int16x4 c) {
  AE_MULASFD32X16_H3_L2(acc, d, c);
  return acc;
}

// CHECK-LABEL: @aa_h0_l1_stmt
// CHECK: call {{.*}}@llvm.haydn.f2mulaa32rs.hhll
ae_int64 aa_h0_l1_stmt(ae_int64 acc, ae_int16x4 d, ae_int16x4 c) {
  AE_MULAAFD32X16_H0_L1(acc, d, c);
  return acc;
}

// CHECK-LABEL: @aa_h2_l3_stmt
// CHECK: call {{.*}}@llvm.haydn.f2mulaa32rs.hhll
ae_int64 aa_h2_l3_stmt(ae_int64 acc, ae_int16x4 d, ae_int16x4 c) {
  AE_MULAAFD32X16_H2_L3(acc, d, c);
  return acc;
}
