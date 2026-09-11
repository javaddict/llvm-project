// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE %s
//
// REQUIRES: haydn-registered-target
//
// NatureDSP compat tier taxonomy: residual silent-wrong maps are tagged
// UNSUPPORTED; exact XC peers remain usable under default fail-closed mode.
// With __HAYDN_ALLOW_INEXACT_AE transitional inexact residual bodies still
// compile (NatureDSP -c only).
// AE_MULZAAFD16SS_33_22 EXACT; AE_L16X4_RIC EXACT (neg D_LDW_CB stride);
// AE_LA16X4_RIC / AE_LA32X2_RIC EXACT (UA dir=1 + neg CBR wrap);
// AE_L16_XC EMULATED (i16 load + soft CBR step). Permanent residual:
// AE_ADD64X2_ / AE_ADD64X2_vector UNSUPPORTED (no bag dual-64).
// Residual class: SELP24 / SEL24 / dual-24 NEG|NEGSP|ADD|SUB /
// SRAI24|SRAIP24|F24X2_SRAI|ADDP24|ZERO24 EXACT; L32X2_RIC / L32X2F24_RIC /
// LA32X2F24_RIC EXACT; dual-24 unaligned circular LA32X2F24_{IC,XC}/
// SA32X2F24_{IC,XC} + LA32X2F24POS_PC EXACT (AR residual + soft CBR);
// LA*POS_PC / LA*NEG_PC EXACT (PLDWWUA seed; dir on RIC/RIP);
// reverse linear RIP loads/stores EXACT; saturating left-shift aliases
// (SLAI64S/SLAS64S/F64_SLAIS/SLAI{32,16}S/SLAI24S/SLAS32S/F32X2_SLAIS)
// EMULATED; AE_MAXABS16S EMULATED (X4ABS16S+X4MAX16, not maxabs32s);
// AE_SRAS32/AE_SLAS32 SAR dual shifts EXACT; AE_SRA64_32 EMULATED;
// ADDSP24S/SUBSP24S EXACT dual-24.
// Value/ref bar: haydn-compat-exact-value-ref.c +
// llvm/test/CodeGen/Haydn/compat/ae-selp24-neg24-ric-satshift.c +
// llvm/test/CodeGen/Haydn/compat/ae-rip-slas64s-f64slais.c +
// llvm/test/CodeGen/Haydn/compat/ae-la-sa-ar-rewire.c +
// llvm/test/CodeGen/Haydn/compat/ae-maxabs16s-neg-pc.c.

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
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2_ permanent UNSUPPORTED (no bag dual-64)");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2 permanent UNSUPPORTED (no bag dual-64; C4.2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULC32X16_H == HAYDN_COMPAT_EXACT,
               "MULC32X16_H exact haydn_x2cmul32x16_h");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULC32X16_L == HAYDN_COMPAT_EXACT,
               "MULC32X16_L exact haydn_x2cmul32x16_l");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAAAAQ16 == HAYDN_COMPAT_EMULATED,
               "MULAAAAQ16 unconditional Path-A haydn_fmulaa16_hs_11_00");
/* Quad-16 max-abs composite (never 2x32 maxabs32s). */
_Static_assert(HAYDN_COMPAT_TIER_AE_MAXABS16S == HAYDN_COMPAT_EMULATED,
               "MAXABS16S emulated X4ABS16S+X4MAX16");
/* Unaligned circular seeds: POS/NEG both PLDWWUA (dir on RIC/RIP). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4POS_PC == HAYDN_COMPAT_EXACT,
               "LA16X4POS_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2POS_PC == HAYDN_COMPAT_EXACT,
               "LA32X2POS_PC exact PLDWWUA seed");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4NEG_PC == HAYDN_COMPAT_EXACT,
               "LA16X4NEG_PC exact same seed as POS (dir on RIC)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2NEG_PC == HAYDN_COMPAT_EXACT,
               "LA32X2NEG_PC exact same seed as POS (dir on RIC)");

/* Residual class: SELP24 / SEL24 + dual-24 + reverse dual-32 CB. */
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HH == HAYDN_COMPAT_EXACT,
               "SELP24_HH exact X2SEL32_HH (not OR)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HL == HAYDN_COMPAT_EXACT,
               "SELP24_HL exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LH == HAYDN_COMPAT_EXACT,
               "SELP24_LH exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LL == HAYDN_COMPAT_EXACT,
               "SELP24_LL exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_HH == HAYDN_COMPAT_EXACT,
               "SEL24_HH exact X2SEL32_HH (not bag OR)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_HL == HAYDN_COMPAT_EXACT,
               "SEL24_HL exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LH == HAYDN_COMPAT_EXACT,
               "SEL24_LH exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LL == HAYDN_COMPAT_EXACT,
               "SEL24_LL exact");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG24S == HAYDN_COMPAT_EXACT,
               "NEG24S exact dual-lane X2NEG32S");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEGSP24S == HAYDN_COMPAT_EXACT,
               "NEGSP24S exact dual-lane (alias NEG24S; not scalar neg32s)");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD24S == HAYDN_COMPAT_EXACT,
               "ADD24S exact dual-lane X2ADD32S");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB24S == HAYDN_COMPAT_EXACT,
               "SUB24S exact dual-lane X2SUB32S");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDP24 == HAYDN_COMPAT_EXACT,
               "ADDP24 exact dual-lane X2ADD32 (not scalar add)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI24 == HAYDN_COMPAT_EXACT,
               "SRAI24 exact dual-lane X2SRA32 (not scalar >>)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAIP24 == HAYDN_COMPAT_EXACT,
               "SRAIP24 exact dual-lane alias of SRAI24");
_Static_assert(HAYDN_COMPAT_TIER_AE_F24X2_SRAI == HAYDN_COMPAT_EXACT,
               "F24X2_SRAI exact dual-lane X2SRA32");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO24 == HAYDN_COMPAT_EXACT,
               "ZERO24 exact dual-24 zero bag");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_RIC == HAYDN_COMPAT_EXACT,
               "L32X2_RIC exact negative CB stride");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "L32X2F24_RIC exact negative CB stride");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIC exact reverse UA + neg CBR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI64S == HAYDN_COMPAT_EMULATED,
               "SLAI64S emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED,
               "SLAA64S emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS64S == HAYDN_COMPAT_EMULATED,
               "SLAS64S emulated sat left (SAR/explicit; not ASR)");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAIS == HAYDN_COMPAT_EMULATED,
               "F64_SLAIS emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAS == HAYDN_COMPAT_EMULATED,
               "F64_SLAS emulated soft sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA32S == HAYDN_COMPAT_EMULATED,
               "SLAA32S emulated per-lane sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA16S == HAYDN_COMPAT_EMULATED,
               "SLAA16S emulated per-lane sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI32S == HAYDN_COMPAT_EMULATED,
               "SLAI32S emulated per-lane sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI16S == HAYDN_COMPAT_EMULATED,
               "SLAI16S emulated per-lane sat left");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI24S == HAYDN_COMPAT_EMULATED,
               "SLAI24S emulated dual-lane soft sat left (not scalar)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED,
               "SLAS32S emulated bidirectional sat (not always ASR)");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED,
               "F32X2_SLAIS emulated dual sat left (not wrap X2SLL)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAS32 == HAYDN_COMPAT_EXACT,
               "SRAS32 exact dual ASR by SAR (not scalar half)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32 == HAYDN_COMPAT_EXACT,
               "SLAS32 exact dual left by SAR (not sat SLAS32S)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRA64_32 == HAYDN_COMPAT_EMULATED,
               "SRA64_32 emulated pack/sat narrow");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDSP24S == HAYDN_COMPAT_EXACT,
               "ADDSP24S exact dual-24 (alias ADD24S)");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUBSP24S == HAYDN_COMPAT_EXACT,
               "SUBSP24S exact dual-24 (alias SUB24S)");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_RIP == HAYDN_COMPAT_EXACT,
               "S32X2_RIP exact reverse linear store");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "S32X2F24_RIP exact reverse linear store");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "L32X2F24_RIP exact reverse linear load");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_RIP == HAYDN_COMPAT_EXACT,
               "L16X4_RIP exact reverse linear load");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_RIP == HAYDN_COMPAT_EXACT,
               "L32X2_RIP exact reverse linear load");
/* Reverse unaligned post-inc residual (UA dir=1; not forward IP). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIP == HAYDN_COMPAT_EXACT,
               "LA16X4_RIP exact reverse UA post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2_RIP exact reverse UA post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIP exact reverse dual-24 UA post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA16X4_RIP == HAYDN_COMPAT_EXACT,
               "SA16X4_RIP exact reverse UA post-inc store");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2_RIP exact reverse UA post-inc store");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2F24_RIP exact reverse dual-24 UA post-inc store");
/* Dual-24 unaligned forward IP residual (AR step). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_IP == HAYDN_COMPAT_EXACT,
               "LA32X2F24_IP exact dual-24 AR post-inc");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_IP == HAYDN_COMPAT_EXACT,
               "SA32X2F24_IP exact dual-24 AR post-inc store");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2_IP == HAYDN_COMPAT_EXACT,
               "LA24X2_IP exact dual-24 IP alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA24X2_IP == HAYDN_COMPAT_EXACT,
               "SA24X2_IP exact dual-24 IP alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24POS_PC == HAYDN_COMPAT_EXACT,
               "LA32X2F24POS_PC exact AR seed (not no-op)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2POS_PC == HAYDN_COMPAT_EXACT,
               "LA24X2POS_PC exact AR seed alias");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_IC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_IC exact AR residual + soft CBR");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_IC == HAYDN_COMPAT_EXACT,
               "SA32X2F24_IC exact AR residual + soft CBR");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_XC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_XC exact AR residual + soft CBR stride");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_XC == HAYDN_COMPAT_EXACT,
               "SA32X2F24_XC exact AR residual + soft CBR stride");

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
  /* dual-32 reverse-CB (negative stride). */
  AE_L32X2_RIC(d32x2, p32x2, 16, 0);
  /* C4.2 EXACT reverse-IC (UA dir=1 + neg CBR wrap). */
  AE_LA16X4_RIC(d16x4, al16, p16x4, 0);
  AE_LA32X2_RIC(d32x2, al32, p32x2, 0);
  {
    ae_f24x2 d24 = 0;
    ae_f24x2 *p24 = (ae_f24x2 *)(void *)p32x2;
    AE_LA32X2F24_RIC(d24, al32, p24, 0);
    /* Dual-24 unaligned circular: seed + IC/XC (not plain mem / aligned CB). */
    AE_LA32X2F24POS_PC(al32, p24);
    AE_LA32X2F24_IC(d24, al32, p24, 0);
    AE_SA32X2F24_IC(d24, al32, p24, 0);
    AE_LA32X2F24_XC(d24, al32, p24, 8, 0);
    AE_SA32X2F24_XC(d24, al32, p24, 8, 0);
    (void)d24;
  }
  /* C4.2 EMULATED scalar L16 circular (soft i16 + CBR step). */
  AE_L16_XC(d16, p16, 16, 0);
  (void)d32x2;
  (void)d16x4;
  (void)d16;
}

/* SELP24 / SEL24 + dual-24 ALU/shift + sat left under default fail-closed. */
void exact_selp24_dual24_surface(ae_f24x2 a, ae_f24x2 b, ae_int64 q) {
  ae_f24x2 s = AE_SELP24_HH(a, b);
  s = AE_SELP24_HL(s, a);
  s = AE_SELP24_LH(s, b);
  s = AE_SELP24_LL(s, a);
  s = AE_SEL24_HH(s, a);
  s = AE_SEL24_HL(s, b);
  s = AE_SEL24_LH(s, a);
  s = AE_SEL24_LL(s, b);
  s = AE_NEG24S(s);
  s = AE_NEGSP24S(s);
  s = AE_ADD24S(s, a);
  s = AE_SUB24S(s, b);
  s = AE_ADDSP24S(s, a);
  s = AE_SUBSP24S(s, b);
  s = AE_ADDP24(s, a);
  s = AE_SRAI24(s, 1);
  s = AE_SRAIP24(s, 1);
  s = AE_F24X2_SRAI(s, 1);
  s = AE_SLAI24S(s, 1);
  s = AE_SEL24_HH(s, AE_ZERO24());
  q = AE_SLAI64S(q, 1);
  q = AE_SLAA64S(q, 1);
  q = AE_SLAS64S(q, 1);
  WUR_AE_SAR(1);
  q = AE_SLAS64S(q);
  q = (ae_int64)AE_F64_SLAIS((ae_f64)q, 1);
  /* Bidirectional dual-32 sat by SAR/explicit; dual F32 sat left. */
  {
    ae_int32x2 x = {0x40000000, 0x40000000};
    WUR_AE_SAR(1);
    x = AE_SLAS32S(x);
    x = AE_SLAS32S(x, 1);
    x = (ae_int32x2)AE_F32X2_SLAIS((ae_f32x2)x, 1);
    /* SAR dual non-sat: ASR / SLL by ambient or explicit amount. */
    WUR_AE_SAR(1);
    x = AE_SRAS32(x);
    x = AE_SRAS32(x, 1);
    x = AE_SLAS32(x);
    x = AE_SLAS32(x, 1);
    (void)AE_SRA64_32((ae_int64)0, 0);
    (void)x;
  }
  (void)s;
  (void)q;
}

/* Reverse linear RIP load/store surface (not forward IP). */
void exact_rip_surface(ae_int32x2 *p32, ae_f24x2 *pf24, ae_int16x4 *p16) {
  ae_int32x2 d32 = {0, 0};
  ae_f24x2 d24 = 0;
  ae_int16x4 d16 = {0};
  ae_valign al16 = AE_ZALIGN64();
  ae_valign al32 = AE_ZALIGN64();
  AE_L32X2_RIP(d32, p32, 8);
  AE_S32X2_RIP(d32, p32, 8);
  AE_L32X2F24_RIP(d24, pf24, 8);
  AE_S32X2F24_RIP(d24, pf24, 8);
  AE_L16X4_RIP(d16, p16, 8);
  /* Reverse unaligned post-inc residual (UA dir=1). */
  AE_LA16X4_RIP(d16, al16, p16, 8);
  AE_LA32X2_RIP(d32, al32, p32, 8);
  AE_LA32X2F24_RIP(d24, al32, pf24, 8);
  AE_SA16X4_RIP(d16, al16, p16, 8);
  AE_SA32X2_RIP(d32, al32, p32);
  AE_SA32X2F24_RIP(d24, al32, pf24);
  /* Dual-24 unaligned forward IP residual. */
  AE_LA32X2F24_IP(d24, al32, pf24);
  AE_SA32X2F24_IP(d24, al32, pf24);
  AE_LA24X2_IP(d24, al32, pf24);
  AE_SA24X2_IP(d24, al32, pf24);
  (void)d32;
  (void)d24;
  (void)d16;
  (void)al16;
  (void)al32;
}

/* C4.2: exact MULZAAFD16SS_33_22 available under default fail-closed mode. */
void exact_mul33_surface(ae_int16x4 d16x4) {
  /* 2-arg zero-acc form returns a value; 3-arg form is an assignment stmt. */
  ae_int64 acc = AE_MULZAAFD16SS_33_22(d16x4, d16x4);
  AE_MULZAAFD16SS_33_22(acc, d16x4, d16x4);
  (void)acc;
}

/* EMULATED MAXABS16S + EXACT NEG_PC seed under default fail-closed. */
void exact_maxabs16s_neg_pc_surface(ae_int16x4 *p16, ae_int32x2 *p32) {
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
  (void)acc;
  (void)a16;
  (void)a32;
}

#if defined(__HAYDN_ALLOW_INEXACT_AE)
/* Transitional NatureDSP -c only: permanent-UNSUPPORTED body is knowingly
 * inexact (scalar i64 cross-lane carry). Not EXACT/EMULATED. */
void inexact_residual_surface(void) {
  ae_int64x2 sum = AE_ADD64X2_vector((ae_int64x2)0, (ae_int64x2)0);
  ae_int64 s2 = AE_ADD64X2_((ae_int64)0, (ae_int64)0);
  (void)sum;
  (void)s2;
}
#endif
