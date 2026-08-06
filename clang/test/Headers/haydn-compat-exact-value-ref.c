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
//
// AE_LA16X4_RIC / AE_LA32X2_RIC were on this list (UA dir=1 ImmArg + cbr -8).
// They are WITHDRAWN (§ 8 Q2) — the AR direction select is gone — so there is
// no value/ref bar left to meet and their probes are removed. Their forward
// _IC contrast probes STAY: those are what prove the remaining forward path
// still passes dir = 0, which is the property the withdrawal must not blur.
// haydn-compat-la-ric.c asserts the withdrawal itself.
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
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_UNSUPPORTED,
               "LA16X4_RIC withdrawn (§ 8 Q2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_UNSUPPORTED,
               "LA32X2_RIC withdrawn (§ 8 Q2)");
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
// Forward XC peer: positive element stride +2 for byte offs 16.
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 2
// ASM-LABEL: l16x4_xc_forward_contrast
// ASM: d_ldw_cb_imm
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

// The O0 checks moved here from the withdrawn la16x4_ric_known_dir1 probe.
// They are what keeps the -O0 RUN line meaningful, and the operand they pin —
// the trailing dir argument — is exactly the one whose other value (1) no
// longer has hardware.
// IR-LABEL: @la16x4_ic_forward_contrast
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 0
// O0-LABEL: @la16x4_ic_forward_contrast
// O0: call {{.*}}@haydn_ae_la16x4_step({{.*}}i32 noundef 8, i32 noundef 0)
// ASM-LABEL: la16x4_ic_forward_contrast
// ASM: d_lqhwua_post
ae_int16x4 la16x4_ic_forward_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_IC(d, al, p, 0);
  return d;
}

