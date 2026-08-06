// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -DVERIFY_WITHDRAWN -verify %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -DVERIFY_WITHDRAWN -D__HAYDN_ALLOW_INEXACT_AE -verify %s
//
// The withdrawn-macro uses sit behind -DVERIFY_WITHDRAWN so the IR run above
// still has something to compile: a translation unit that fails to parse
// emits no IR, and the forward-IC contrast is the half worth checking.
//
// REQUIRES: haydn-registered-target
//
// AE_LA16X4_RIC / AE_LA32X2_RIC are WITHDRAWN (FORMAT-E-SWITCH-PLAN.md § 8 Q2).
//
// They were EXACT reverse-IC: the unaligned reverse path
// (haydn_ae_la{16x4,64}_step / d_l{qhw,tw}ua_post with dir = 1) plus
// haydn_cbr_step(ptr, -8, cbr_sel) for the circular wrap. The re-delivered ISA
// dropped the AR direction select, so there is no reverse instruction to lower
// to any more.
//
// This test used to assert the exact lowering. It now asserts the withdrawal,
// because the property that mattered is unchanged and is what the old header
// comment called out: RIC must not silent-alias forward IC (dir = 0, +8).
// Failing to compile is the strongest form of that guarantee, and it is worth
// testing — a future "restore" that quietly maps RIC onto the forward path
// would otherwise pass every other test in this directory.
//
// The second and third RUN lines pin that __HAYDN_ALLOW_INEXACT_AE does NOT
// re-enable these. That flag is the opt-out for inexact MAPPINGS; this is
// missing HARDWARE, so it must not apply.

#include <haydn_dsp.h>

// The tier tag moves with the macro. It was HAYDN_COMPAT_EXACT.
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_UNSUPPORTED,
               "LA16X4_RIC is withdrawn, not exact (§ 8 Q2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_UNSUPPORTED,
               "LA32X2_RIC is withdrawn, not exact (§ 8 Q2)");

// Forward IC is UNAFFECTED — it passes dir = 0 and stride 8, which the
// hardware still has. This is the contrast case the old test carried, and it
// is the reason the withdrawal has to be loud rather than an alias.
// IR-LABEL: @la16x4_ic_contrast
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 0)
ae_int16x4 la16x4_ic_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_IC(d, al, p, 0);
  return d;
}

#ifdef VERIFY_WITHDRAWN

// Both arities of both macros are withdrawn: the 3-arg form only defaulted
// cbr_sel to 0, it still needed dir = 1.
ae_int16x4 la16x4_ric_4arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  // expected-error@+1 {{static assertion failed}}
  AE_LA16X4_RIC(d, al, p, 0);
  return d;
}

ae_int16x4 la16x4_ric_3arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  // expected-error@+1 {{static assertion failed}}
  AE_LA16X4_RIC(d, al, p);
  return d;
}

ae_int32x2 la32x2_ric_4arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  // expected-error@+1 {{static assertion failed}}
  AE_LA32X2_RIC(d, al, p, 0);
  return d;
}

ae_int32x2 la32x2_ric_3arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  // expected-error@+1 {{static assertion failed}}
  AE_LA32X2_RIC(d, al, p);
  return d;
}

#endif // VERIFY_WITHDRAWN
