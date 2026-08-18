// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only %s
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only -DTEST_MAP_FP_FAIL_CLOSED %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MAPFP
// RUN: not clang -target haydn-unknown-elf -ffreestanding -fsyntax-only %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=GENERIC
//
// MAP-FP / COMPOSITE / HDR-SPLIT measured KPI slice after the M1 harness.
// Miss (not a Stage-0 revive):
//   MAP-FP — float AE families stay in haydn_dsp.h; X2CMUL/MULFC wrappers
//            stay fail-closed (SOURCE-REVALIDATE, no invented lane map).
//   COMPOSITE — FIR/FFT remain C-level compositions of golden members.
//   HDR-SPLIT — one haydn_dsp.h; no float-family header split.
// Empty output is failure. Default fail-closed (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

/* Quad-16 64-bit MAC stays unnamed so vec_dot16 takes AE_MULAF16X4SS. */
#if defined(AE_MULAAAAQ16)
_Static_assert(0, "AE_MULAAAAQ16 must be undefined under default strict");
#endif

/* COMPOSITE miss: FIR helpers stay C-level inlines, not a native ISA. */
#if defined(AE_FIR_NATIVE_COMPOSITE) || defined(AE_FFT_NATIVE_COMPOSITE)
_Static_assert(0, "do not invent a native FIR/FFT composite ISA");
#endif

/* HDR-SPLIT miss: stay one public header; no invented family split. */
#if defined(__HAYDN_DSP_FP_H) || defined(__HAYDN_DSP_HDR_SPLIT)
_Static_assert(0, "do not split haydn_dsp.h into family headers");
#endif

#ifndef TEST_MAP_FP_FAIL_CLOSED
/* Inventory labels exist; bodies are fail-closed. */
_Static_assert(HAYDN_COMPAT_TIER_AE_CMUL32_F2 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULC32X16_H == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULFC24RA == HAYDN_COMPAT_EMULATED, "");
#endif

#ifdef TEST_MAP_FP_FAIL_CLOSED
// MAPFP: __haydn_ae_unsupported_AE_MULC32X16_H
ae_int32x2 mapfp_mulc32x16_h(ae_int32x2 a, ae_int32x2 b) {
  return AE_MULC32X16_H(a, b);
}
// MAPFP: __haydn_ae_unsupported_AE_MULC32X16_L
ae_int32x2 mapfp_mulc32x16_l(ae_int32x2 a, ae_int32x2 b) {
  return AE_MULC32X16_L(a, b);
}
// MAPFP: __haydn_ae_unsupported_AE_MULFC24RA
ae_f24x2 mapfp_mulfc24ra(ae_f24x2 a, ae_f24x2 b) { return AE_MULFC24RA(a, b); }
// MAPFP: silent-wrong map removed): AE_CMUL32_F2
void mapfp_cmul32_f2(ae_int32x2 *d0, ae_int32x2 *d1, ae_int32x2 a,
                     ae_int32x2 b) {
  AE_CMUL32_F2(*d0, *d1, a, b);
}
#endif

// GENERIC: needs target feature simd
