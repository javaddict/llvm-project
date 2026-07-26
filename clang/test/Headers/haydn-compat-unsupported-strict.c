// RUN: not %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -DTEST_ADD64X2 %s 2>&1 | FileCheck %s --check-prefix=ADD64
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
//
// REQUIRES: haydn-registered-target
//
// C4.1 / G-DSP-COMPAT: residual silent-wrong public AE maps fail closed under
// the default (strict) mode. No silent alias to a different direction, width,
// lane, or arithmetic. C4.2 repaired AE_MULZAAFD16SS_33_22 (exact hs_33_22),
// AE_L16X4_RIC (exact negative D_LDW_CB stride), AE_LA*_RIC (exact UA dir=1 +
// neg CBR wrap), and AE_L16_XC (emulated i16 + soft CBR step). Permanent
// residual: AE_ADD64X2_vector only (no bag dual-64; not EMULATED). Lit lock:
// haydn-compat-add64x2-residual.c. Transitional NatureDSP -c must opt in via
// __HAYDN_ALLOW_INEXACT_AE (tier-taxonomy + add64x2-residual).

#include <haydn_dsp.h>

#if defined(TEST_ADD64X2)
ae_int64x2 use_add64x2(ae_int64x2 a, ae_int64x2 b) {
  return AE_ADD64X2_vector(a, b);
}
// ADD64: __haydn_ae_unsupported_AE_ADD64X2_vector
#else
/* C4.2: AE_L16_XC is public under strict (EMULATED) — must compile. */
void use_l16_xc(ae_int16 *p) {
  ae_int16 d;
  AE_L16_XC(d, p, 16, 0);
  (void)d;
}
#endif
