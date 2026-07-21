// RUN: %clang_cc1 -triple haydn-unknown-elf -O2 -emit-llvm -ffreestanding -o - %s | FileCheck %s
//
// REQUIRES: haydn-registered-target
//
// Contract: AE_MULSSFD32* dual MSU macros must update the accumulator
// (return of __haydn_fmuls32s_* / f2mulss must not be discarded).
//
// CHECK-LABEL: @mulss_s_hhll
// CHECK: call {{.*}}@llvm.haydn.fmuls32s.hh
// CHECK: call {{.*}}@llvm.haydn.fmuls32s.ll
//
// CHECK-LABEL: @mulss_r_hhll
// CHECK: call {{.*}}@llvm.haydn.f2mulss32rs.hhll
//
// CHECK-LABEL: @mulss_r_hllh
// CHECK: call {{.*}}@llvm.haydn.f2mulss32rs.hllh

#include <haydn_dsp.h>

ae_int64 mulss_s_hhll(ae_int64 acc, ae_int32x2 x, ae_int32x2 y) {
  AE_MULSSFD32S_HH_LL(acc, x, y);
  return acc;
}
ae_int64 mulss_r_hhll(ae_int64 acc, ae_int32x2 x, ae_int32x2 y) {
  AE_MULSSFD32R_HH_LL(acc, x, y);
  return acc;
}
ae_int64 mulss_r_hllh(ae_int64 acc, ae_int32x2 x, ae_int32x2 y) {
  AE_MULSSFD32R_HL_LH(acc, x, y);
  return acc;
}
