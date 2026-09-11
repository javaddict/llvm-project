// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// D1.73 / D1.111: AE_MULAAAAQ16 is unconditional Path-A
// haydn_fmulaa16_hs_11_00 (golden FMULAA16_HS_11_00, two-lane 11+00).
// Presence-only flips are not the value oracle. This file pins:
//   - defined under default strict (no __HAYDN_ALLOW_INEXACT_AE)
//   - EMULATED / emu.mulaaaaq16
//   - IR+ASM of the flipped body (fmulaa16.hs.11.00, not x4mula16s)
//   - NatureDSP `#ifndef AE_MULAAAAQ16` kernel class takes Path-A
//   - two-lane integer host 380 of the Wave-8 vector (0+1 per quad)
// Dest-typed Path-B 1190 stays AE_MULAF16X4SS (vec-dot16-mula16s.c /
// ae-tdsp3-exact-kernels.c). Do not retarget that host onto this name.

#include <haydn_dsp.h>

_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
#ifndef AE_MULAAAAQ16
_Static_assert(0, "AE_MULAAAAQ16 must be defined unconditionally");
#endif
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAAAAQ16 == HAYDN_COMPAT_EMULATED,
               "MULAAAAQ16 Path-A EMULATED");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULAAAAQ16,
                                "emu.mulaaaaq16") == 0,
               "MULAAAAQ16 oracle");

// Documented two-lane integer host of the Wave-8 vector (lanes 0+1 per
// quad). Full-quad integer host is 1190 (dest-typed Path-B).
// IR-LABEL: @two_lane_host_380
// IR: ret i32 380
int32_t two_lane_host_380(void) {
  const int16_t x[8] = {10, -10, 20, -20, 30, 40, 50, 60};
  const int16_t y[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int acc = 0;
  for (int i = 0; i < 8; i += 4)
    acc = haydn_satsr64((int64_t)acc + (int64_t)x[i] * (int64_t)y[i] +
                            (int64_t)x[i + 1] * (int64_t)y[i + 1],
                        0);
  return acc;
}

// Known low-lane vectors: product body is hs.11.00, not X4MULA16S.
// IR-LABEL: @mulaaaaq16_known_low_lanes
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00(
// IR-SAME: i64 0,
// IR-SAME: <4 x i16> <i16 4, i16 2, i16 0, i16 0>,
// IR-SAME: <4 x i16> <i16 5, i16 3, i16 0, i16 0>
// IR-NOT: x4mula16s
// ASM-LABEL: mulaaaaq16_known_low_lanes
// ASM: fmulaa16.hs.11.00
// ASM-NOT: x4mula16s
ae_int64 mulaaaaq16_known_low_lanes(void) {
  ae_int16x4 a = {4, 2, 0, 0};
  ae_int16x4 b = {5, 3, 0, 0};
  ae_int64 acc = 0;
  AE_MULAAAAQ16(acc, a, b);
  return acc;
}

// NatureDSP vec_dot16x16_fast_hifi3 class (`#ifndef AE_MULAAAAQ16` at
// corpus :77, `#else` AE_MULAAAAQ16 at :136). Product law defines the
// name, so this compiles Path-A.
// IR-LABEL: @guarded_kernel_vec_dot16_class
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00
// IR-NOT: x4mula16s
// ASM-LABEL: guarded_kernel_vec_dot16_class
// ASM: fmulaa16.hs.11.00
// ASM-NOT: x4mula16s
int32_t guarded_kernel_vec_dot16_class(ae_int16x4 x, ae_int16x4 y) {
#ifndef AE_MULAAAAQ16
  ae_f32x2 vaf = AE_MOVI(0);
  ae_f32x2 vbf = AE_MOVI(0);
  AE_MULAF16X4SS(vaf, vbf, x, y);
  ae_int32x2 vai = (ae_int32x2)vaf;
  ae_int32x2 vbi = (ae_int32x2)vbf;
  vai = AE_ADD32S(vai, vbi);
  vbi = AE_SEL32_LH(vai, vai);
  vai = AE_ADD32S(vai, vbi);
  return AE_MOVAD32_H(vai);
#else
  ae_int64 acc = AE_ZERO64();
  AE_MULAAAAQ16(acc, x, y);
  ae_int32x2 t = AE_TRUNCA32X2F64S(acc, acc, 33);
  return AE_MOVAD32_L(t);
#endif
}
