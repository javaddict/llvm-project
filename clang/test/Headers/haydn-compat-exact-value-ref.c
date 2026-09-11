// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// C4.2 / G-DSP-COMPAT exit bar — value/ref probes for every EXACT residual AE
// wrapper repaired in C4.2:
//   AE_MULZAAFD16SS_33_22  — dual-high hs_33_22 (not silent _11_00)
//   AE_L16X4_RIC           — neg D_LDW_CB stride (not forward XC)
//   AE_LA16X4_RIC / AE_LA32X2_RIC — UA dir=1 ImmArg + cbr -8 (not forward IC)
//
// Style: addbrba32 known-value probe (intr-addbrba32.ll) — host-documented
// oracles in comments + known-vector/imm IR+ASM contrast. Default fail-closed
// mode (no __HAYDN_ALLOW_INEXACT_AE). No BundleSim product ctest (not a CAPI
// gate). Hexagon peer = fail-closed/feature-gated C surface (not AE tiers).
//
// Host oracles (Database/golden FMULAA16_HS_* Behavior; Python/int64):
//   FMULAA16_HS_33_22:
//     rtd[63:32] = SATQ1.31(acc[63:32]
//                   + SATQ1.31(rsd1[63:48]*rsd2[63:48])   // lane 3
//                   + SATQ1.31(rsd1[47:32]*rsd2[47:32])); // lane 2
//     rtd[31:0]  unchanged.
//   FMULAA16_HS_11_00: same shape on lanes 1+0 into rtd[63:32].
//
// Known vector (high-only lanes; low lanes zero):
//   a = {0, 0, 4, 2}  // lane0..3 → bits [15:0]..[63:48]
//   b = {0, 0, 5, 3}
//   zero-acc _33_22 oracle:
//     p3 = 2*3 = 6, p2 = 4*5 = 20, sum = 26 (no sat)
//     result bag = (int64_t)26 << 32  (== 0x0000001a00000000)
//   same vector through _11_00 → 0 (lanes 1+0 zero) — proves silent alias
//   would drop the dual-high products.

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_MULZAAFD16SS_33_22 == HAYDN_COMPAT_EXACT,
               "MULZAAFD16SS_33_22 EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_RIC == HAYDN_COMPAT_EXACT,
               "L16X4_RIC EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_EXACT,
               "LA16X4_RIC EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2_RIC EXACT");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "default fail-closed");

/* Host-documented oracle constant: (26 << 32) for zero-acc high-only probe.
 * Consumed only as a live constant so DCE cannot drop the MAC result. */
#define HAYDN_ORACLE_MUL33_HIGH_ONLY ((ae_int64)0x1a00000000LL)

//===----------------------------------------------------------------------===//
// AE_MULZAAFD16SS_33_22 — known-vector dual-high vs silent low-lane contrast
//===----------------------------------------------------------------------===//

// IR-LABEL: @mul33_known_high_only_zero_acc
// Known high-only vectors must lower to hs.33.22 (never silent hs.11.00).
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22(
// IR-SAME: i64 0,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 4, i16 2>,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 5, i16 3>
// IR-NOT: fmulaa16.hs.11.00
// ASM-LABEL: mul33_known_high_only_zero_acc
// ASM: fmulaa16.hs.33.22
// ASM-NOT: fmulaa16.hs.11.00
ae_int64 mul33_known_high_only_zero_acc(void) {
  /* lane0=0, lane1=0, lane2=4, lane3=2  /  lane2=5, lane3=3 */
  ae_int16x4 a = {0, 0, 4, 2};
  ae_int16x4 b = {0, 0, 5, 3};
  ae_int64 r = AE_MULZAAFD16SS_33_22(a, b);
  /* Keep oracle live so the result cannot be pure-DCE'd away in O2 if the
   * intrinsic were ever mis-modelled Const with a wrong constant fold. */
  return r ^ (ae_int64)HAYDN_ORACLE_MUL33_HIGH_ONLY
             ^ (ae_int64)HAYDN_ORACLE_MUL33_HIGH_ONLY;
}

// IR-LABEL: @mul33_known_high_only_with_acc
// 3-arg form: same dual-high intrinsic; acc flows as first arg.
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.33.22(
// IR-SAME: i64 7,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 4, i16 2>,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 5, i16 3>
// IR-NOT: fmulaa16.hs.11.00
// ASM-LABEL: mul33_known_high_only_with_acc
// ASM: fmulaa16.hs.33.22
ae_int64 mul33_known_high_only_with_acc(void) {
  ae_int16x4 a = {0, 0, 4, 2};
  ae_int16x4 b = {0, 0, 5, 3};
  ae_int64 acc = 7; /* low half only; HS accumulates into [63:32] */
  AE_MULZAAFD16SS_33_22(acc, a, b);
  return acc;
}

// Contrast oracle: low-only lanes through native _11_00 (must NOT be what
// _33_22 lowers to). Same host shape: p1=2*3 + p0=4*5 = 26 → high half.
// IR-LABEL: @mul11_known_low_only_contrast
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00(
// IR-SAME: i64 0,
// IR-SAME: <4 x i16> <i16 4, i16 2, i16 0, i16 0>,
// IR-SAME: <4 x i16> <i16 5, i16 3, i16 0, i16 0>
// IR-NOT: fmulaa16.hs.33.22
// ASM-LABEL: mul11_known_low_only_contrast
// ASM: fmulaa16.hs.11.00
// ASM-NOT: fmulaa16.hs.33.22
ae_int64 mul11_known_low_only_contrast(void) {
  ae_int16x4 a = {4, 2, 0, 0}; /* lanes 0+1 */
  ae_int16x4 b = {5, 3, 0, 0};
  return haydn_fmulaa16_hs_11_00(0, __AE_TO_I64(a), __AE_TO_I64(b));
}

// High-only vectors through _11_00 would yield 0 — documents why silent
// alias of _33_22→_11_00 is value-wrong (oracle sum 26 disappears).
// IR-LABEL: @mul11_high_only_would_zero
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00(
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 4, i16 2>,
// IR-SAME: <4 x i16> <i16 0, i16 0, i16 5, i16 3>
// ASM-LABEL: mul11_high_only_would_zero
// ASM: fmulaa16.hs.11.00
ae_int64 mul11_high_only_would_zero(void) {
  ae_int16x4 a = {0, 0, 4, 2};
  ae_int16x4 b = {0, 0, 5, 3};
  return haydn_fmulaa16_hs_11_00(0, __AE_TO_I64(a), __AE_TO_I64(b));
}

//===----------------------------------------------------------------------===//
// AE_L16X4_RIC — reverse-CB known stride (neg D_LDW_CB element units)
//===----------------------------------------------------------------------===//
// Host oracle (cross-arch Phase B §7 / ImmCheckSimm8):
//   byte inc → element stride = -((inc) >> 3); post-inc by imm<<3 with wrap.
//   inc=16 → stride -2; inc=8 → stride -1. Forward XC is +((inc)>>3).

// IR-LABEL: @l16x4_ric_known_neg2
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -2
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 2
// ASM-LABEL: l16x4_ric_known_neg2
// ASM: d_ldw_cb_imm
ae_int16x4 *l16x4_ric_known_neg2(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_RIC(d, p, 16, 0);
  (void)d;
  return p;
}

// IR-LABEL: @l16x4_xc_forward_contrast
// Forward XC peer: reg form, raw byte stride 16 in a GPR (single law for
// constant and variable strides; imm form is the RIC/reverse path only).
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg
// ASM-LABEL: l16x4_xc_forward_contrast
// ASM: d_ldw_cb_reg
ae_int16x4 *l16x4_xc_forward_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_XC(d, p, 16, 0);
  (void)d;
  return p;
}

//===----------------------------------------------------------------------===//
// AE_LA*_RIC — reverse-IC known dir=1 + cbr step -8
//===----------------------------------------------------------------------===//
// Host oracle: UA reverse path dir ImmArg=1 (same as LA*_RIP); circular wrap
// via haydn_cbr_step(ptr, -8, cbr_sel). Forward IC is dir=0 and soft +8.

// IR-LABEL: @la16x4_ric_known_dir1
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1
// O0-LABEL: @la16x4_ric_known_dir1
// O0: call {{.*}}@haydn_ae_la16x4_step({{.*}}i32 noundef 8, i32 noundef 1)
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef -8
// ASM-LABEL: la16x4_ric_known_dir1
// ASM: d_lqhwua_post
ae_int16x4 la16x4_ric_known_dir1(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_RIC(d, al, p, 0);
  return d;
}

// IR-LABEL: @la16x4_ic_forward_contrast
// Forward IC routes via haydn_ae_cb_ld_tw (c96c5cdef2dd): unaligned AR
// window load with HW circular +8 post step — not the dir=0 RIC path above.
// IR: call {{.*}}@llvm.haydn.lqhwua.cb.post(ptr {{[^,]+}}, i32 1, i32 0
// ASM-LABEL: la16x4_ic_forward_contrast
// ASM: d_lqhwua_cb_post
ae_int16x4 la16x4_ic_forward_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_IC(d, al, p, 0);
  return d;
}

// IR-LABEL: @la32x2_ric_known_dir1
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1
// O0-LABEL: @la32x2_ric_known_dir1
// O0: call {{.*}}@haydn_ae_la64_step({{.*}}i32 noundef 8, i32 noundef 1)
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef -8
// ASM-LABEL: la32x2_ric_known_dir1
// ASM: d_ltwua_post
ae_int32x2 la32x2_ric_known_dir1(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIC(d, al, p, 0);
  return d;
}

//===----------------------------------------------------------------------===//
// D1.15: AE_LA32X2_RIP / AE_SA32X2_RIP — reverse UA lane law
//===----------------------------------------------------------------------===//
// Host oracle (golden instruction_type_index.json type AR):
//   D_LTWUA_POST: temp=mem64[rs&~7]; window={temp,ar};
//   rtd=(rs[2]==0)?window[63:00]:window[95:32]; ar=temp; rs=rs+8 — NO dir
//   operand: direction only steps the pointer, data word order is
//   direction-independent. D_STWUA_POST: rs[2]==0 → mem64=rtd;
//   rs[2]==1 → mem64={rtd[31:00],ar[31:00]}, ar[31:00]=rtd[63:32].
// Therefore RIP owes the same H-first presentation as IP/IC/RIC: the raw
// LE window goes through haydn_ae_f32x2_mem_to_reg ((u>>32)|(u<<32));
// stores swap src before haydn_ae_sa64_step. At O2 the swap lowers to
// llvm.fshl.i64(x, x, 32); at O0 the concrete helper calls remain.

// IR-LABEL: @la32x2_rip_known_dir1_swap
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// O0-LABEL: @la32x2_rip_known_dir1_swap
// O0: call {{.*}}@haydn_ae_la64_step({{.*}}i32 noundef 8, i32 noundef 1)
// O0: call {{.*}}@haydn_ae_f32x2_mem_to_reg
// ASM-LABEL: la32x2_rip_known_dir1_swap
// ASM: d_ltwua_post
ae_int32x2 la32x2_rip_known_dir1_swap(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIP(d, al, p);
  return d;
}

// IR-LABEL: @la32x2f24_rip_known_dir1_swap
// Dual-24 reverse UA load: same dir=1 + swap (keeps ae_f24x2 cast).
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2f24_rip_known_dir1_swap
// ASM: d_ltwua_post
ae_f24x2 la32x2f24_rip_known_dir1_swap(ae_f24x2 *p) {
  ae_f24x2 d = (ae_f24x2)0;
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2F24_RIP(d, al, p, 8);
  return d;
}

// O0-LABEL: @sa32x2_rip_known_swap_before_step
// Store arm: src is swapped BEFORE the UA step; dir stays 1.
// O0: call {{.*}}@haydn_ae_f32x2_mem_to_reg
// O0: call {{.*}}@haydn_ae_sa64_step({{.*}}i32 noundef 8, i32 noundef 1)
// IR-LABEL: @sa32x2_rip_known_swap_before_step
// Constant {0x200000001-ish} pair folds through the swap: {1,2} bag
// 0x0000000200000001 → swapped 0x0000000100000002 = 4294967298.
// IR: call {{.*}}@llvm.haydn.d.stwua.post(i64 4294967298,{{.*}}i32 {{[0-3]}}, i32 8, i32 1)
// ASM-LABEL: sa32x2_rip_known_swap_before_step
// ASM: d_stwua_post
ae_int32x2 *sa32x2_rip_known_swap_before_step(ae_int32x2 *p) {
  ae_int32x2 v = {1, 2};
  ae_valign al = AE_ZALIGN64();
  AE_SA32X2_RIP(v, al, p);
  return p;
}
