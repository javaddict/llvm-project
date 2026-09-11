// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR

// Role: semantic — Public AE compatibility value/ref probes for residual maps:
// AE_SELP24_* / AE_SEL24_* — dual-24 lane select via X2SEL32 (not bag OR).

// Public AE compatibility value/ref probes for residual maps:
//   AE_SELP24_*       — dual-24 lane select via X2SEL32 (not bag OR)
//   AE_SEL24_*        — same X2SEL32 pack (not bag bitwise OR)
//   AE_NEG24S /
//   AE_NEGSP24S       — dual-lane X2NEG32S (not scalar neg32s)
//   AE_SRAI24 /
//   AE_SRAIP24 /
//   AE_F24X2_SRAI     — dual-lane X2SRA32 (not scalar >> high-lane drop)
//   AE_ADDP24         — dual-lane X2ADD32 (not scalar +)
//   AE_SLAI24S        — dual-lane soft sat left (not scalar sla32s_lane)
//   AE_L32X2_RIC /
//   AE_L32X2F24_RIC   — reverse-CB negative D_LDW_CB stride (not forward XC)
//   AE_SLAI64S        — soft saturating left (not plain SLAI64 wrap)
//
// Default fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HH == HAYDN_COMPAT_EXACT, "SELP24_HH");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HL == HAYDN_COMPAT_EXACT, "SELP24_HL");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LH == HAYDN_COMPAT_EXACT, "SELP24_LH");
_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_LL == HAYDN_COMPAT_EXACT, "SELP24_LL");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_HH == HAYDN_COMPAT_EXACT, "SEL24_HH");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LL == HAYDN_COMPAT_EXACT, "SEL24_LL");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG24S == HAYDN_COMPAT_EXACT, "NEG24S");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEGSP24S == HAYDN_COMPAT_EXACT, "NEGSP24S");
_Static_assert(HAYDN_COMPAT_TIER_AE_F24X2_SRAI == HAYDN_COMPAT_EXACT, "F24X2_SRAI");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI24 == HAYDN_COMPAT_EXACT, "SRAI24");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAIP24 == HAYDN_COMPAT_EXACT, "SRAIP24");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDP24 == HAYDN_COMPAT_EXACT, "ADDP24");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD24S == HAYDN_COMPAT_EXACT, "ADD24S");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_RIC == HAYDN_COMPAT_EXACT, "L32X2_RIC");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIC == HAYDN_COMPAT_EXACT, "L32X2F24_RIC");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI24S == HAYDN_COMPAT_EMULATED, "SLAI24S");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI64S == HAYDN_COMPAT_EMULATED, "SLAI64S");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED, "SLAA64S");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Host oracle (X2SEL32_HH): result.H = a.H, result.L = b.H.
// Silent OR of bags would set bits from both whole words — not a lane pack.
// IR-LABEL: @selp24_hh_lowers_to_x2sel32
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// IR-NOT: or i64
// ASM-LABEL: selp24_hh_lowers_to_x2sel32:
// ASM: x2sel32_hh
ae_f24x2 selp24_hh_lowers_to_x2sel32(ae_f24x2 a, ae_f24x2 b) {
  return AE_SELP24_HH(a, b);
}

// IR-LABEL: @selp24_ll_lowers_to_x2sel32
// IR: call {{.*}}@llvm.haydn.x2sel32.ll
// ASM-LABEL: selp24_ll_lowers_to_x2sel32:
// ASM: x2sel32_ll
ae_f24x2 selp24_ll_lowers_to_x2sel32(ae_f24x2 a, ae_f24x2 b) {
  return AE_SELP24_LL(a, b);
}

// IR-LABEL: @selp24_hl_lh
// IR-DAG: call {{.*}}@llvm.haydn.x2sel32.hl
// IR-DAG: call {{.*}}@llvm.haydn.x2sel32.lh
// ASM-LABEL: selp24_hl_lh:
// ASM-DAG: x2sel32_hl
// ASM-DAG: x2sel32_lh
// ASM: x2sel32_hh
ae_f24x2 selp24_hl_lh(ae_f24x2 a, ae_f24x2 b) {
  ae_f24x2 h = AE_SELP24_HL(a, b);
  ae_f24x2 l = AE_SELP24_LH(a, b);
  return AE_SELP24_HH(h, l);
}

// Dual-24 sat negate must be X2NEG32S (both lanes), never scalar neg32s.
// IR-LABEL: @neg24s_dual_lane
// IR: call {{.*}}@llvm.haydn.x2neg32s
// IR-NOT: call {{.*}}@llvm.haydn.neg32s(
// ASM-LABEL: neg24s_dual_lane:
// ASM: x2neg32s
ae_f24x2 neg24s_dual_lane(ae_f24x2 a) { return AE_NEG24S(a); }

// AE_NEGSP24S is dual-lane alias of NEG24S (not scalar __haydn_neg32s).
// IR-LABEL: @negsp24s_dual_lane
// IR: call {{.*}}@llvm.haydn.x2neg32s
// IR-NOT: call {{.*}}@llvm.haydn.neg32s(
// ASM-LABEL: negsp24s_dual_lane:
// ASM: x2neg32s
ae_f24x2 negsp24s_dual_lane(ae_f24x2 a) { return AE_NEGSP24S(a); }

// AE_SEL24_* same X2SEL32 pack as SELP24 — not bag OR (mtx/vec NatureDSP).
// IR-LABEL: @sel24_hh_lowers_to_x2sel32
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// IR-NOT: or i64
// ASM-LABEL: sel24_hh_lowers_to_x2sel32:
// ASM: x2sel32_hh
ae_f24x2 sel24_hh_lowers_to_x2sel32(ae_f24x2 a, ae_f24x2 b) {
  return AE_SEL24_HH(a, b);
}

// IR-LABEL: @sel24_ll_hl
// IR-DAG: call {{.*}}@llvm.haydn.x2sel32.ll
// IR-DAG: call {{.*}}@llvm.haydn.x2sel32.hl
// ASM-LABEL: sel24_ll_hl:
// ASM-DAG: x2sel32_ll
// ASM-DAG: x2sel32_hl
ae_f24x2 sel24_ll_hl(ae_f24x2 a, ae_f24x2 b) {
  ae_f24x2 l = AE_SEL24_LL(a, b);
  ae_f24x2 h = AE_SEL24_HL(a, b);
  return AE_SEL24_HH(h, l);
}

// Dual-24 sat add/sub — X2* not scalar.
// IR-LABEL: @add24s_dual_lane
// IR: call {{.*}}@llvm.haydn.x2add32s
// IR-NOT: call {{.*}}@llvm.haydn.add32s(
// ASM-LABEL: add24s_dual_lane:
// ASM: x2add32s
ae_f24x2 add24s_dual_lane(ae_f24x2 a, ae_f24x2 b) { return AE_ADD24S(a, b); }

// Dual-24 non-sat add — X2ADD32 (both lanes), never scalar (a)+(b).
// IR-LABEL: @addp24_dual_lane
// IR: call {{.*}}@llvm.haydn.x2add32
// IR-NOT: add i32
// ASM-LABEL: addp24_dual_lane:
// ASM: x2add32
ae_f24x2 addp24_dual_lane(ae_f24x2 a, ae_f24x2 b) { return AE_ADDP24(a, b); }

// Dual-24 ASR — X2SRA32; scalar (int)a>>s would drop the high lane.
// IR-LABEL: @srai24_dual_lane
// IR: call {{.*}}@llvm.haydn.x2sra32
// IR-NOT: ashr i32
// ASM-LABEL: srai24_dual_lane:
// ASM: x2sra32
ae_f24x2 srai24_dual_lane(ae_f24x2 a) { return AE_SRAI24(a, 1); }

// IR-LABEL: @sraip24_dual_lane
// IR: call {{.*}}@llvm.haydn.x2sra32
// ASM-LABEL: sraip24_dual_lane:
// ASM: x2sra32
ae_f24x2 sraip24_dual_lane(ae_f24x2 a) { return AE_SRAIP24(a, 1); }

// IR-LABEL: @f24x2_srai_dual_lane
// IR: call {{.*}}@llvm.haydn.x2sra32
// ASM-LABEL: f24x2_srai_dual_lane:
// ASM: x2sra32
ae_f24x2 f24x2_srai_dual_lane(ae_f24x2 a) { return AE_F24X2_SRAI(a, 2); }

// Dual-24 sat left: soft per-lane; compile surface under strict + both lanes.
// Host oracle: AE_SLAI24S of (0x40000000, 0x40000000)<<1 saturates each lane
// to INT32_MAX (plain << wraps to INT32_MIN on both lanes).
// Bag packing: lo | (hi<<32) → 0x7fffffff7fffffff.
// IR-LABEL: @slai24s_sat_known
// IR: ret i64 9223372034707292159
// ASM-LABEL: slai24s_sat_known:
ae_f24x2 slai24s_sat_known(void) {
  /* Dual bag: lo=0x40000000, hi=0x40000000 → sat-left-1 → 0x7fffffff each. */
  ae_f24x2 a = (ae_f24x2)(haydn_dr64_t)0x4000000040000000ULL;
  return AE_SLAI24S(a, 1);
}

// ZERO24 is a dual-24 zero bag usable as SEL24 lane source.
// IR-LABEL: @zero24_sel_hh
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// ASM-LABEL: zero24_sel_hh:
// ASM: x2sel32_hh
ae_f24x2 zero24_sel_hh(ae_f24x2 a) { return AE_SEL24_HH(a, AE_ZERO24()); }

// Reverse dual-32 CB: byte offs 16 → element stride -2 (not +2).
// IR-LABEL: @l32x2_ric_neg2
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -2
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 2
// ASM-LABEL: l32x2_ric_neg2:
// ASM: d_ldw_cb_imm
// ASM-SAME: -2
ae_int32x2 *l32x2_ric_neg2(ae_int32x2 *p) {
  ae_int32x2 d = {0, 0};
  AE_L32X2_RIC(d, p, 16, 0);
  (void)d;
  return p;
}

// Forward XC contrast keeps positive stride. XC byte-stride law: forward
// XC macros lower via ldw.cb.reg with the RAW byte stride (16), not the
// imm element form (haydn_dsp.h AE_L32X2_XC).
// IR-LABEL: @l32x2_xc_pos2
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg{{.*}}i32 0, i32 16
// ASM-LABEL: l32x2_xc_pos2:
// ASM: d_ldw_cb_reg
ae_int32x2 *l32x2_xc_pos2(ae_int32x2 *p) {
  ae_int32x2 d = {0, 0};
  AE_L32X2_XC(d, p, 16, 0);
  (void)d;
  return p;
}

// F24 reverse-CB same negative stride contract.
// IR-LABEL: @l32x2f24_ric_neg1
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -1
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 1
// ASM-LABEL: l32x2f24_ric_neg1:
// ASM: d_ldw_cb_imm
// ASM-SAME: -1
ae_f24x2 *l32x2f24_ric_neg1(ae_f24x2 *p) {
  ae_f24x2 d = 0;
  AE_L32X2F24_RIC(d, p, 8, 0);
  (void)d;
  return p;
}

// Host oracle: AE_SLAI64S(0x4000000000000000LL, 1) saturates to INT64_MAX
// (plain << wraps to INT64_MIN). Keep oracle live against DCE.
#define HAYDN_ORACLE_SLAI64S_SAT ((ae_int64)0x7FFFFFFFFFFFFFFFLL)

// IR-LABEL: @slai64s_saturates_known
// Constant-folded soft sat → INT64_MAX.
// IR: ret i64 9223372036854775807
// ASM-LABEL: slai64s_saturates_known:
ae_int64 slai64s_saturates_known(void) {
  ae_int64 r = AE_SLAI64S((ae_int64)0x4000000000000000LL, 1);
  /* XOR with oracle twice is identity — keeps the constant-fold result live. */
  return r ^ HAYDN_ORACLE_SLAI64S_SAT ^ HAYDN_ORACLE_SLAI64S_SAT;
}

// IR-LABEL: @slaa32s_dual_surface
// Dual sat left must compile under strict; exercise both lanes.
ae_int32x2 slaa32s_dual_surface(ae_int32x2 a, int s) {
  return AE_SLAA32S(a, s);
}
