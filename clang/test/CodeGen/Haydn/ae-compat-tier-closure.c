// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE %s
// RUN: python3 %S/ae-compat-tier-audit.py \
// RUN:   %S/../../../lib/Headers/haydn_dsp.h \
// RUN:   %S/../../../include/clang/Basic/BuiltinsHaydn.td
//
// REQUIRES: haydn-registered-target
//
// Mechanical AE_* tier taxonomy: every public macro is tagged via
// BuiltinsHaydn.td HaydnAeCompat → haydn.h HAYDN_COMPAT_TIER_*.
// Product law: AE_MAXABS16S EMULATED (X4ABS16S+X4MAX16); AE_ADD64X2_*
// UNSUPPORTED; AE_LA*NEG_PC EXACT probe-only (POS PLDWWUA seed). Fail-closed
// inventory audit rejects untagged macros and invented reverse NEG seed.

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_NATIVE == 0, "native tier ordinal");
_Static_assert(HAYDN_COMPAT_EXACT == 1, "exact tier ordinal");
_Static_assert(HAYDN_COMPAT_EMULATED == 2, "emulated tier ordinal");
_Static_assert(HAYDN_COMPAT_UNSUPPORTED == 3, "unsupported tier ordinal");

/* Closed inventory: full public AE_* surface (not the old ~64 opt-in set).
 * Floor blocks shrink (SRAS32-class untagged hole). Growth requires both
 * haydn_dsp.h and BuiltinsHaydn.td HaydnAeCompat to move together. */
_Static_assert(HAYDN_AE_COMPAT_TAG_COUNT >= 600,
               "AE compat tier inventory must cover the full public AE surface");
/* Named value/object oracle floor: AE-P0 set (and aliases) must not shrink. */
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 8,
               "AE value/object oracle inventory floor");
/* Permanent dual-64 quarantine is the only UNSUPPORTED pair under product law. */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED &&
                   HAYDN_COMPAT_TIER_AE_ADD64X2_vector ==
                       HAYDN_COMPAT_UNSUPPORTED,
               "closed UNSUPPORTED set is ADD64X2_* only");

/* Product law: MAXABS16S EMULATED composite (never maxabs32s). */
_Static_assert(HAYDN_COMPAT_TIER_AE_MAXABS16S == HAYDN_COMPAT_EMULATED,
               "MAXABS16S emulated X4ABS16S+X4MAX16");

_Static_assert(HAYDN_COMPAT_TIER_AE_TRUNCA32X2F64S == HAYDN_COMPAT_EMULATED, "TRUNCA");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVTQ56A32S == HAYDN_COMPAT_EMULATED, "CVTQ56");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVT16X4 == HAYDN_COMPAT_EMULATED, "CVT16");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVT16X4_1ARG == HAYDN_COMPAT_EMULATED, "CVT16_1");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED, "SLAA64S");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED, "SA64NEG");
/* Typed public inventory: AE-P0 oracle/availability + store-finish immediates/effects. */
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_TRUNCA32X2F64S, "ae0.trunca32x2f64s") == 0, "trunca oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVTQ56A32S, "ae0.cvtq56a32s") == 0, "cvtq56 oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVT16X4, "ae0.cvt16x4") == 0, "cvt16 oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVT16X4_1ARG, "ae0.cvt16x4_1arg") == 0, "cvt16_1 oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA64S, "ae0.slaa64s") == 0, "slaa64s oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SA64POS_FP, "ae0.sa64pos_fp") == 0, "sa64pos oracle");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SA64NEG_FP, "ae0.sa64neg_fp") == 0, "sa64neg oracle");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64POS_FP == 0, "SA64POS dir0");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64NEG_FP == 1, "SA64NEG dir1");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_SA64POS_FP == 1, "SA64POS mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_SA64NEG_FP == 1, "SA64NEG mem");
#if defined(__HAYDN_ALLOW_INEXACT_AE)
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 0, "inexact transitional mode");
#else
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
#endif


/* Product law: dual-64 add permanent UNSUPPORTED. */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2_ permanent UNSUPPORTED");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2_vector permanent UNSUPPORTED");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULC32X16_H == HAYDN_COMPAT_EXACT &&
                   HAYDN_COMPAT_TIER_AE_MULC32X16_L == HAYDN_COMPAT_EXACT,
               "MULC32X16_* exact X2CMUL32X16 (not X2CMUL32)");
/* Scalar AE_ADD64 stays usable; dual-64 quarantine must not sweep it. */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64 == HAYDN_COMPAT_EMULATED,
               "ADD64 scalar bag add stays EMULATED");

/* Curated EXACT peers still tagged. */
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAS32 == HAYDN_COMPAT_EXACT,
               "SRAS32 exact dual ASR by SAR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32 == HAYDN_COMPAT_EXACT,
               "SLAS32 exact dual left by SAR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HH == HAYDN_COMPAT_EXACT,
               "SELP24_HH exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HL == HAYDN_COMPAT_EXACT,
               "SELP24_HL exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LH == HAYDN_COMPAT_EXACT,
               "SELP24_LH exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LL == HAYDN_COMPAT_EXACT,
               "SELP24_LL exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_HH == HAYDN_COMPAT_EXACT,
               "SEL24_HH exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LL == HAYDN_COMPAT_EXACT,
               "SEL24_LL exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_HH == HAYDN_COMPAT_EXACT,
               "SEL32_HH exact dual-32 lane pack peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_HL == HAYDN_COMPAT_EXACT,
               "SEL32_HL exact dual-32 lane pack peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_LH == HAYDN_COMPAT_EXACT,
               "SEL32_LH exact dual-32 lane pack peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_LL == HAYDN_COMPAT_EXACT,
               "SEL32_LL exact dual-32 lane pack peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG24S == HAYDN_COMPAT_EXACT,
               "NEG24S exact dual-lane sat neg");
_Static_assert(HAYDN_COMPAT_TIER_AE_F24X2_SRAI == HAYDN_COMPAT_EXACT,
               "F24X2_SRAI exact dual ASR");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SRAI == HAYDN_COMPAT_EXACT,
               "F32X2_SRAI exact dual ASR peer of F24");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI24 == HAYDN_COMPAT_EXACT,
               "SRAI24 exact dual ASR alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAIP24 == HAYDN_COMPAT_EXACT,
               "SRAIP24 exact dual ASR alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDP24 == HAYDN_COMPAT_EXACT,
               "ADDP24 exact dual add");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD24S == HAYDN_COMPAT_EXACT,
               "ADD24S exact dual sat add");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB24S == HAYDN_COMPAT_EXACT,
               "SUB24S exact dual sat sub");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDSP24S == HAYDN_COMPAT_EXACT,
               "ADDSP24S exact dual sat add alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUBSP24S == HAYDN_COMPAT_EXACT,
               "SUBSP24S exact dual sat sub alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEGSP24S == HAYDN_COMPAT_EXACT,
               "NEGSP24S exact dual sat neg alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO24 == HAYDN_COMPAT_EXACT,
               "ZERO24 exact dual-24 zero bag");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_HL == HAYDN_COMPAT_EXACT,
               "SEL24_HL exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LH == HAYDN_COMPAT_EXACT,
               "SEL24_LH exact lane pack");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_RIC == HAYDN_COMPAT_EXACT,
               "L32X2_RIC exact reverse-CB");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "L32X2F24_RIC exact reverse-CB");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIC exact reverse-UA");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_IC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_IC exact dual-24 AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_IC == HAYDN_COMPAT_EXACT,
               "SA32X2F24_IC exact dual-24 AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2_IC == HAYDN_COMPAT_EXACT,
               "LA24X2_IC exact dual-24 IC alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA24X2_IC == HAYDN_COMPAT_EXACT,
               "SA24X2_IC exact dual-24 IC alias");
/* Base unaligned circular residual peers of dual-24 IC (same AR+CBR path). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_IC == HAYDN_COMPAT_EXACT,
               "LA16X4_IC exact AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_IC == HAYDN_COMPAT_EXACT,
               "LA32X2_IC exact AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA16X4_IC == HAYDN_COMPAT_EXACT,
               "SA16X4_IC exact AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2_IC == HAYDN_COMPAT_EXACT,
               "SA32X2_IC exact AR+CBR IC");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_XC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_XC exact dual-24 AR+CBR XC");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_XC == HAYDN_COMPAT_EXACT,
               "SA32X2F24_XC exact dual-24 AR+CBR XC");
/* Reverse unaligned post-inc residual (UA dir=1). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIP == HAYDN_COMPAT_EXACT,
               "LA16X4_RIP exact reverse UA post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2_RIP exact reverse UA post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIP exact reverse dual-24 UA");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA16X4_RIP == HAYDN_COMPAT_EXACT,
               "SA16X4_RIP exact reverse UA store");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2_RIP exact reverse UA store");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2F24_RIP exact reverse dual-24 UA store");
/* Dual-24 unaligned forward IP residual. */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_IP == HAYDN_COMPAT_EXACT,
               "LA32X2F24_IP exact dual-24 AR post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_IP == HAYDN_COMPAT_EXACT,
               "SA32X2F24_IP exact dual-24 AR post-inc store");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2_IP == HAYDN_COMPAT_EXACT,
               "LA24X2_IP exact dual-24 IP alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA24X2_IP == HAYDN_COMPAT_EXACT,
               "SA24X2_IP exact dual-24 IP alias");
/* Dual-24 aligned F24 XC residual: same D_LDW/SDW_CB as base L/S32X2_XC. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_XC == HAYDN_COMPAT_EXACT,
               "L32X2F24_XC exact dual-24 aligned CB load");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2F24_XC == HAYDN_COMPAT_EXACT,
               "S32X2F24_XC exact dual-24 aligned CB store");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_XC == HAYDN_COMPAT_EXACT,
               "L32X2_XC exact aligned CB load peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_XC == HAYDN_COMPAT_EXACT,
               "S32X2_XC exact aligned CB store peer");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4NEG_PC == HAYDN_COMPAT_EXACT,
               "LA16X4NEG_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2NEG_PC == HAYDN_COMPAT_EXACT,
               "LA32X2NEG_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4POS_PC == HAYDN_COMPAT_EXACT,
               "LA16X4POS_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2POS_PC == HAYDN_COMPAT_EXACT,
               "LA32X2POS_PC exact PLDWWUA seed");
/* Dual-24 F24 POS seed peers: same PLDWWUA probe-only law as base POS/NEG. */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24POS_PC == HAYDN_COMPAT_EXACT,
               "LA32X2F24POS_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2POS_PC == HAYDN_COMPAT_EXACT,
               "LA24X2POS_PC exact dual-24 POS alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24POS_PC ==
                   HAYDN_COMPAT_TIER_AE_LA32X2POS_PC,
               "F24 POS seed tier must equal base 32x2 POS");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO24 == HAYDN_COMPAT_EXACT,
               "ZERO24 exact dual-24 zero bag");
/* Store-finish residual: POS dir0 / NEG dir1 (not LA*NEG_PC seed alias). */
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64POS_FP == HAYDN_COMPAT_EMULATED,
               "SA64POS_FP emulated store-finish dir0");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED,
               "SA64NEG_FP emulated store-finish dir1");

/* Closed UNSUPPORTED cardinality lock (ADD64X2_* pair only). */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED &&
                   HAYDN_COMPAT_TIER_AE_ADD64X2_vector ==
                       HAYDN_COMPAT_UNSUPPORTED &&
                   HAYDN_COMPAT_TIER_AE_ADD64 != HAYDN_COMPAT_UNSUPPORTED,
               "UNSUPPORTED set is dual-64 only; scalar ADD64 stays usable");

/* EMULATED composite still present. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L16_XC == HAYDN_COMPAT_EMULATED,
               "L16_XC emulated soft CBR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI64S == HAYDN_COMPAT_EMULATED,
               "SLAI64S emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI24S == HAYDN_COMPAT_EMULATED,
               "SLAI24S emulated soft dual-24 sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED,
               "SLAS32S emulated soft sat left by SAR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED,
               "SLAA64S emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS64S == HAYDN_COMPAT_EMULATED,
               "SLAS64S emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAIS == HAYDN_COMPAT_EMULATED,
               "F64_SLAIS emulated soft sat left (IIR hot path)");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED,
               "F32X2_SLAIS emulated dual soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAS == HAYDN_COMPAT_EMULATED,
               "F64_SLAS emulated soft sat left");

/* Surface still usable under default fail-closed. */
void ae_compat_tier_surface(ae_int16x4 *p16, ae_int32x2 *p32) {
  ae_int16x4 acc = {0, 0, 0, 0};
  ae_int16x4 v = {1, -2, 3, -4};
  acc = AE_MAXABS16S(acc, v);
  acc = AE_MAXABS16S(v);
  ae_valign a16 = AE_ZALIGN64();
  ae_valign a32 = AE_ZALIGN64();
  AE_LA16X4NEG_PC(a16, p16);
  AE_LA32X2NEG_PC(a32, p32);
  AE_LA16X4POS_PC(a16, p16);
  AE_LA32X2POS_PC(a32, p32);
  {
    ae_valign a24 = AE_ZALIGN64();
    const ae_f24x2 *pf = (const ae_f24x2 *)p32;
    AE_LA32X2F24POS_PC(a24, pf);
    AE_LA24X2POS_PC(a24, pf);
    (void)a24;
  }
  WUR_AE_SAR(1);
  ae_int32x2 x = {0x40000000, 0x40000000};
  x = AE_SRAS32(x);
  x = AE_SLAS32(x);
  x = AE_SLAS32S(x);
  ae_f24x2 a = AE_ZERO24();
  ae_f24x2 b = AE_ZERO24();
  a = AE_SELP24_HH(a, b);
  a = AE_SEL24_LL(a, b);
  x = AE_SEL32_HH(x, x);
  a = AE_NEG24S(a);
  a = AE_ADD24S(a, b);
  a = AE_SUB24S(a, b);
  a = AE_ADDP24(a, b);
  a = AE_F24X2_SRAI(a, 1);
  a = AE_SRAI24(a, 1);
  a = AE_SLAI24S(a, 1);
  {
    ae_valign a24 = AE_ZALIGN64();
    ae_f24x2 *pf = (ae_f24x2 *)p32;
    AE_LA32X2F24_IC(a, a24, pf, 0);
    AE_LA24X2_IC(a, a24, pf, 0);
    AE_SA32X2F24_IC(a, a24, pf, 0);
    AE_SA24X2_IC(a, a24, pf, 0);
    AE_LA32X2_IC(x, a24, p32, 0);
    AE_SA32X2_IC(x, a24, p32, 0);
    AE_LA16X4_IC(v, a24, p16, 0);
    AE_SA16X4_IC(v, a24, p16, 0);
    (void)a24;
  }
  ae_int64 q = AE_SLAI64S((ae_int64)1, 2);
  ae_f64 fq = AE_F64_SLAIS((ae_f64)q, 1);
  fq = AE_F64_SLAS(fq, 1);
  ae_f32x2 fx = AE_F32X2_SLAIS(x, 1);
  fx = AE_F32X2_SRAI(fx, 1);
  (void)acc;
  (void)a16;
  (void)a32;
  (void)x;
  (void)a;
  (void)q;
  (void)fq;
  (void)fx;
}
