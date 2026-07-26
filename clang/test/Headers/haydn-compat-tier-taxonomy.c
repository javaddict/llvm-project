// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE %s
//
// REQUIRES: haydn-registered-target
//
// C4.1 / G-DSP-COMPAT: NatureDSP compat tier taxonomy is published, residual
// silent-wrong maps are tagged UNSUPPORTED, and exact XC peers remain usable
// under the default fail-closed mode. With __HAYDN_ALLOW_INEXACT_AE the
// transitional inexact residual bodies still compile (NatureDSP -c only).
// C4.2: AE_MULZAAFD16SS_33_22 EXACT; AE_L16X4_RIC EXACT (neg D_LDW_CB stride);
// AE_LA16X4_RIC / AE_LA32X2_RIC EXACT (UA dir=1 + neg CBR wrap);
// AE_L16_XC EMULATED (i16 load + soft CBR step). Permanent residual:
// AE_ADD64X2_vector UNSUPPORTED (no bag dual-64).
// Value/ref bar for EXACT wrappers: haydn-compat-exact-value-ref.c (CAPI-4 exit).

#include <haydn_dsp.h>

/* Tier enum values are stable integers for client #if checks. */
_Static_assert(HAYDN_COMPAT_NATIVE == 0, "native tier ordinal");
_Static_assert(HAYDN_COMPAT_EXACT == 1, "exact tier ordinal");
_Static_assert(HAYDN_COMPAT_EMULATED == 2, "emulated tier ordinal");
_Static_assert(HAYDN_COMPAT_UNSUPPORTED == 3, "unsupported tier ordinal");

/* Exact forward XC peers. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_XC == HAYDN_COMPAT_EXACT,
               "L32X2_XC is exact CB wrap");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_XC == HAYDN_COMPAT_EXACT,
               "S32X2_XC is exact CB wrap");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_XC == HAYDN_COMPAT_EXACT,
               "L16X4_XC is exact CB wrap");
_Static_assert(HAYDN_COMPAT_TIER_AE_S16X4_XC == HAYDN_COMPAT_EXACT,
               "S16X4_XC is exact CB wrap");

/* C4.2 exact: dual-high-lane MAC (no silent _11_00 alias). */
_Static_assert(HAYDN_COMPAT_TIER_AE_MULZAAFD16SS_33_22 == HAYDN_COMPAT_EXACT,
               "_33_22 exact haydn_fmulaa16_hs_33_22 (C4.2)");

/* C4.2 exact: reverse-CB via signed negative D_LDW_CB stride. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_RIC == HAYDN_COMPAT_EXACT,
               "L16X4_RIC exact negative CB stride (C4.2)");

/* C4.2 exact: LA reverse-IC via UA dir=1 + haydn_cbr_step(ptr,-8). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_EXACT,
               "LA16X4_RIC exact reverse UA + neg CBR (C4.2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2_RIC exact reverse UA + neg CBR (C4.2)");

/* C4.2 emulated: scalar L16 circular via soft i16 + CBR mirrors. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L16_XC == HAYDN_COMPAT_EMULATED,
               "L16_XC emulated i16 load + soft CBR step (C4.2)");

/* Permanent tier UNSUPPORTED — dual-64 lane-wise add has no Haydn bag map. */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2 permanent UNSUPPORTED (no bag dual-64; C4.2)");

#if defined(__HAYDN_ALLOW_INEXACT_AE)
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 0, "inexact opt-in is non-strict");
#else
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "default mode is fail-closed");
#endif

/* Exact XC + reverse-CB / reverse-IC + emulated L16_XC under strict. */
void exact_xc_surface(ae_int32x2 *p32x2, ae_int16x4 *p16x4, ae_int16 *p16) {
  ae_int32x2 d32x2 = {0};
  ae_int16x4 d16x4 = {0};
  ae_int16 d16 = 0;
  ae_valign al16 = AE_ZALIGN64();
  ae_valign al32 = AE_ZALIGN64();
  AE_L32X2_XC(d32x2, p32x2, 16, 0);
  AE_S32X2_XC(d32x2, p32x2, 16, 0);
  AE_L16X4_XC(d16x4, p16x4, 16, 0);
  AE_S16X4_XC(d16x4, p16x4, 16, 0);
  /* C4.2 EXACT reverse-CB (negative stride). */
  AE_L16X4_RIC(d16x4, p16x4, 16, 0);
  /* C4.2 EXACT reverse-IC (UA dir=1 + neg CBR wrap). */
  AE_LA16X4_RIC(d16x4, al16, p16x4, 0);
  AE_LA32X2_RIC(d32x2, al32, p32x2, 0);
  /* C4.2 EMULATED scalar L16 circular (soft i16 + CBR step). */
  AE_L16_XC(d16, p16, 16, 0);
  (void)d32x2;
  (void)d16x4;
  (void)d16;
}

/* C4.2: exact MULZAAFD16SS_33_22 available under default fail-closed mode. */
void exact_mul33_surface(ae_int16x4 d16x4) {
  /* 2-arg zero-acc form returns a value; 3-arg form is an assignment stmt. */
  ae_int64 acc = AE_MULZAAFD16SS_33_22(d16x4, d16x4);
  AE_MULZAAFD16SS_33_22(acc, d16x4, d16x4);
  (void)acc;
}

#if defined(__HAYDN_ALLOW_INEXACT_AE)
/* Transitional NatureDSP -c only: permanent-UNSUPPORTED body is knowingly
 * inexact (scalar i64 cross-lane carry). Not EXACT/EMULATED. */
void inexact_residual_surface(void) {
  ae_int64x2 sum = AE_ADD64X2_vector((ae_int64x2)0, (ae_int64x2)0);
  (void)sum;
}
#endif
