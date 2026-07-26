// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// C4.2 / G-DSP-COMPAT: AE_MULZAAFD16SS_33_22 is EXACT — maps to
// haydn_fmulaa16_hs_33_22 / FMULAA16_HS_33_22 (dual-high lanes 3+3/2+2).
// Must not silently alias low-lane fmulaa16_hs_11_00. Available under
// default fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE required).
//
// Host oracle (Database FMULAA16_HS_33_22 Behavior):
//   rtd[63:32] = SATQ1.31(acc[63:32]
//                 + SATQ1.31(lane3*lane3) + SATQ1.31(lane2*lane2));
// Known high-only vector a={0,0,4,2} b={0,0,5,3}: p3=6 p2=20 sum=26
// → bag (26<<32). Same vector on _11_00 → 0 (value-ref contrast).
// Full value/ref suite: haydn-compat-exact-value-ref.c

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_MULZAAFD16SS_33_22 == HAYDN_COMPAT_EXACT,
               "MULZAAFD16SS_33_22 is exact hs_33_22 (C4.2)");

// IR-LABEL: @mul33_2arg_zero_acc
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22
// IR-NOT: fmulaa16.hs.11.00
// ASM-LABEL: mul33_2arg_zero_acc
// ASM: fmulaa16.hs.33.22
// ASM-NOT: fmulaa16.hs.11.00
ae_int64 mul33_2arg_zero_acc(ae_int16x4 a, ae_int16x4 b) {
  return AE_MULZAAFD16SS_33_22(a, b);
}

// IR-LABEL: @mul33_3arg_acc
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22
// IR-NOT: fmulaa16.hs.11.00
// ASM-LABEL: mul33_3arg_acc
// ASM: fmulaa16.hs.33.22
// ASM-NOT: fmulaa16.hs.11.00
ae_int64 mul33_3arg_acc(ae_int64 acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULZAAFD16SS_33_22(acc, a, b);
  return acc;
}

// IR-LABEL: @mul33_expr_return
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22
ae_int64 mul33_expr_return(ae_int64 acc, ae_int16x4 a, ae_int16x4 b) {
  return AE_MULZAAFD16SS_33_22(acc, a, b);
}

// Known-vector value probe (addbrba32_known style): dual-high lanes only.
// IR-LABEL: @mul33_known_vector
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22(
// IR-SAME: i64 0,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 4, i16 2>,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 5, i16 3>
// IR-NOT: fmulaa16.hs.11.00
// ASM-LABEL: mul33_known_vector
// ASM: fmulaa16.hs.33.22
// ASM-NOT: fmulaa16.hs.11.00
ae_int64 mul33_known_vector(void) {
  ae_int16x4 a = {0, 0, 4, 2};
  ae_int16x4 b = {0, 0, 5, 3};
  return AE_MULZAAFD16SS_33_22(a, b);
}
