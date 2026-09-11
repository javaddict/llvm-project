// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
//
// REQUIRES: haydn-registered-target
//
// Residual public AE dual-24 / lane-select / sat-shift class (owned surface).
// Product law: SELP24/SEL24/SEL32 via X2SEL32 (not bag OR); NEG24S/ADD24S/
// SUB24S dual X2 sat ALU; F24/SRAI24/F32X2_SRAI dual X2SRA32; dual-24 IC
// AR+CBR (LA/SA F24_IC + LA/SA24X2_IC); SLAI24S/SLAS32S/SLAI64S soft sat
// left (EMULATED). Fail-closed default (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_SELP24_HH == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL24_LL == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_HH == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_LL == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG24S == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD24S == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB24S == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F24X2_SRAI == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SRAI == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI24 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDP24 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA24X2_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA16X4_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2_IC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI24S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAIS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAS32 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64POS_FP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED, "");

// IR-LABEL: @selp24_hh_x2sel
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// IR-NOT: or i64
ae_f24x2 selp24_hh_x2sel(ae_f24x2 a, ae_f24x2 b) {
  return AE_SELP24_HH(a, b);
}

// IR-LABEL: @sel24_ll_x2sel
// IR: call {{.*}}@llvm.haydn.x2sel32.ll
// IR-NOT: or i64
ae_f24x2 sel24_ll_x2sel(ae_f24x2 a, ae_f24x2 b) {
  return AE_SEL24_LL(a, b);
}

// IR-LABEL: @sel32_hh_x2sel
// Dual-32 lane-pack peer of SELP24 residual class.
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// IR-NOT: or i64
ae_int32x2 sel32_hh_x2sel(ae_int32x2 a, ae_int32x2 b) {
  return AE_SEL32_HH(a, b);
}

// IR-LABEL: @neg24s_x2neg
// IR: call {{.*}}@llvm.haydn.x2neg32s
ae_f24x2 neg24s_x2neg(ae_f24x2 a) {
  return AE_NEG24S(a);
}

// IR-LABEL: @add_sub24s_x2sat
// IR-DAG: call {{.*}}@llvm.haydn.x2add32s
// IR-DAG: call {{.*}}@llvm.haydn.x2sub32s
ae_f24x2 add_sub24s_x2sat(ae_f24x2 a, ae_f24x2 b) {
  ae_f24x2 s = AE_ADD24S(a, b);
  return AE_SUB24S(s, b);
}

// IR-LABEL: @addp24_x2add
// IR: call {{.*}}@llvm.haydn.x2add32
ae_f24x2 addp24_x2add(ae_f24x2 a, ae_f24x2 b) {
  return AE_ADDP24(a, b);
}

// IR-LABEL: @f24_srai_x2sra
// IR: call {{.*}}@llvm.haydn.x2sra32
ae_f24x2 f24_srai_x2sra(ae_f24x2 a) {
  return AE_F24X2_SRAI(a, 3);
}

// IR-LABEL: @srai24_alias_x2sra
// IR: call {{.*}}@llvm.haydn.x2sra32
ae_f24x2 srai24_alias_x2sra(ae_f24x2 a) {
  return AE_SRAI24(a, 2);
}

// IR-LABEL: @f32x2_srai_x2sra
// Exact dual ASR peer of F24X2_SRAI (not soft sat left).
// IR: call {{.*}}@llvm.haydn.x2sra32
// IR-NOT: call {{.*}}@llvm.smax
ae_f32x2 f32x2_srai_x2sra(ae_f32x2 a) {
  return AE_F32X2_SRAI(a, 3);
}

// IR-LABEL: @sras32_x2sra_sar
// Dual ASR by ambient SAR residual (SRAS32 class hygiene).
// IR: call {{.*}}@llvm.haydn.x2sra32
// IR-NOT: ashr i32
ae_int32x2 sras32_x2sra_sar(ae_int32x2 a) {
  WUR_AE_SAR(2);
  return AE_SRAS32(a);
}

// IR-LABEL: @slai24s_soft_sat
// Dual-lane soft sat left (EMULATED inline) — not scalar high-lane drop.
// Sat may be llvm.smax or icmp+select of INT_MIN/MAX after inline.
// IR-DAG: shl
// IR-DAG: select
// IR-NOT: ashr
ae_f24x2 slai24s_soft_sat(ae_f24x2 a) {
  return AE_SLAI24S(a, 4);
}

// IR-LABEL: @slas32s_soft_sat
// Soft sat left by SAR — not non-sat ASR rebind.
// IR-DAG: shl
// IR-DAG: select
// IR-NOT: ashr
ae_int32x2 slas32s_soft_sat(ae_int32x2 a) {
  WUR_AE_SAR(2);
  return AE_SLAS32S(a);
}

// IR-LABEL: @slai64s_soft_sat
// Soft sat left 64 — not plain wrap <<.
// IR: shl
// IR: select
// IR-NOT: ashr
ae_int64 slai64s_soft_sat(ae_int64 q) {
  return AE_SLAI64S(q, 5);
}

// IR-LABEL: @f64_slais_soft_sat
// Hot IIR residual: soft sat left 64, not plain wrap <<.
// IR: shl
// IR: select
ae_f64 f64_slais_soft_sat(ae_f64 q) {
  return AE_F64_SLAIS(q, 1);
}

// IR-LABEL: @f64_slas_soft_sat
// IR: shl
// IR: select
ae_f64 f64_slas_soft_sat(ae_f64 q) {
  return AE_F64_SLAS(q, 2);
}

// IR-LABEL: @f32x2_slais_soft_sat
// Dual-32 soft sat left — not wrap X2SLL.
// IR-DAG: shl
// IR-DAG: select
ae_f32x2 f32x2_slais_soft_sat(ae_f32x2 v) {
  return AE_F32X2_SLAIS(v, 1);
}

// IR-LABEL: @la32x2f24_ic_ar_cbr
// Dual-24 unaligned circular load: hardware AR_CBR path (2026-08-28 remap).
// The wrapped cursor is live (funnel selector is rs[2] of the wrapped value);
// must not silent-alias reverse RIC or the aligned D_LDW_CB.
// IR: call {{.*}}@llvm.haydn.ltwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
void la32x2f24_ic_ar_cbr(ae_f24x2 *dst, ae_valign *al, ae_f24x2 *ptr) {
  AE_LA32X2F24_IC(*dst, *al, ptr, 0);
}

// IR-LABEL: @la24x2_ic_alias_ar_cbr
// LA24X2_IC is the dual-24 alias of LA32X2F24_IC (same hardware path).
// IR: call {{.*}}@llvm.haydn.ltwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
void la24x2_ic_alias_ar_cbr(ae_f24x2 *dst, ae_valign *al, ae_f24x2 *ptr) {
  AE_LA24X2_IC(*dst, *al, ptr, 0);
}

// IR-LABEL: @sa32x2f24_ic_ar_cbr
// Dual-24 unaligned circular store: hardware AR_CBR path; live wrapped cursor.
// IR: call {{.*}}@llvm.haydn.stwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.sdw.cb.imm
void sa32x2f24_ic_ar_cbr(ae_f24x2 src, ae_valign *al, ae_f24x2 *ptr) {
  AE_SA32X2F24_IC(src, *al, ptr, 0);
}

// IR-LABEL: @sa24x2_ic_alias_ar_cbr
// IR: call {{.*}}@llvm.haydn.stwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.sdw.cb.imm
void sa24x2_ic_alias_ar_cbr(ae_f24x2 src, ae_valign *al, ae_f24x2 *ptr) {
  AE_SA24X2_IC(src, *al, ptr, 0);
}

// IR-LABEL: @la32x2_ic_ar_cbr
// Base dual-32 unaligned circular load: hardware AR_CBR path.
// Must not silent-alias reverse RIC or the aligned D_LDW_CB alone.
// IR: call {{.*}}@llvm.haydn.ltwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
void la32x2_ic_ar_cbr(ae_int32x2 *dst, ae_valign *al, ae_int32x2 *ptr) {
  AE_LA32X2_IC(*dst, *al, ptr, 0);
}

// IR-LABEL: @sa32x2_ic_ar_cbr
// Base dual-32 unaligned circular store: hardware AR_CBR path.
// IR: call {{.*}}@llvm.haydn.stwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.sdw.cb.imm
void sa32x2_ic_ar_cbr(ae_int32x2 src, ae_valign *al, ae_int32x2 *ptr) {
  AE_SA32X2_IC(src, *al, ptr, 0);
}

// IR-LABEL: @la16x4_ic_ar_cbr
// Base quad-16 unaligned circular load: hardware AR_CBR path.
// IR: call {{.*}}@llvm.haydn.lqhwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
void la16x4_ic_ar_cbr(ae_int16x4 *dst, ae_valign *al, ae_int16x4 *ptr) {
  AE_LA16X4_IC(*dst, *al, ptr, 0);
}

// IR-LABEL: @sa16x4_ic_ar_cbr
// Base quad-16 unaligned circular store: hardware AR_CBR path.
// IR: call {{.*}}@llvm.haydn.sqhwua.cb.post({{.*}}i32 0, i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.sdw.cb.imm
void sa16x4_ic_ar_cbr(ae_int16x4 src, ae_valign *al, ae_int16x4 *ptr) {
  AE_SA16X4_IC(src, *al, ptr, 0);
}

// IR-LABEL: @l32x2f24_xc_aligned_cb
// Dual-24 aligned circular load residual: D_LDW_CB_REG byte stride (not
// plain mem; imm<<3 scaling does not apply to the reg form).
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg
// IR-NOT: load i64
void l32x2f24_xc_aligned_cb(ae_f24x2 *dst, ae_f24x2 *ptr) {
  AE_L32X2F24_XC(*dst, ptr, 8, 0);
}

// IR-LABEL: @s32x2f24_xc_aligned_cb_ptr
// Dual-24 aligned circular store residual: D_SDW_CB_REG + CBR next-ptr
// writeback. Must not silent-drop the store-only body that left ptr unmoved.
// IR: call {{.*}}@llvm.haydn.sdw.cb.reg
void s32x2f24_xc_aligned_cb_ptr(ae_f24x2 src, ae_f24x2 *ptr) {
  AE_S32X2F24_XC(src, ptr, 8, 0);
}
