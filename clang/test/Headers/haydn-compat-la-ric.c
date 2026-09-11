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
// C4.2 / G-DSP-COMPAT: AE_LA16X4_RIC / AE_LA32X2_RIC are EXACT reverse-IC —
// unaligned reverse path (haydn_ae_la{16x4,64}_step / d_l{qhw,tw}ua_post
// dir=1 ImmArg, same as AE_LA*_RIP) plus haydn_cbr_step(ptr, -8, cbr_sel).
// Must not silent-alias forward IC (dir=0, +8). Available under default
// fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE required).
//
// Host oracle: dir ImmArg=1 (reverse UA); cbr wrap step = -8 bytes. Forward
// IC contrast is dir=0. Full value/ref suite: haydn-compat-exact-value-ref.c

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_EXACT,
               "LA16X4_RIC is exact reverse-IC via UA dir=1 + neg CBR (C4.2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2_RIC is exact reverse-IC via UA dir=1 + neg CBR (C4.2)");

// Contrast: forward IC routes via haydn_ae_cb_ld_tw (c96c5cdef2dd) — the
// unaligned AR window load with HW circular +8 post step (flar primes AR1).
// RIC must not silent-alias this path (RIC keeps d.lqhwua.post dir=1 below).
// IR-LABEL: @la16x4_ic_contrast
// IR: call {{.*}}@llvm.haydn.lqhwua.cb.post(ptr {{[^,]+}}, i32 1, i32 0)
ae_int16x4 la16x4_ic_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_IC(d, al, p, 0);
  return d;
}

// IR-LABEL: @la16x4_ric_4arg
// Reverse UA load: dir ImmArg = 1 (not forward 0); fixed 8B stride.
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// O0-LABEL: @la16x4_ric_4arg
// O0: call {{.*}}@haydn_ae_la16x4_step({{.*}}i32 noundef 8, i32 noundef 1)
// Reverse circular wrap: negative 8-byte CBR step (not forward +8).
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef -8
// ASM-LABEL: la16x4_ric_4arg
// ASM: d_lqhwua_post
ae_int16x4 la16x4_ric_4arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_RIC(d, al, p, 0);
  return d;
}

// IR-LABEL: @la16x4_ric_3arg
// Default cbr_sel=0; still reverse UA (dir=1).
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// ASM-LABEL: la16x4_ric_3arg
// ASM: d_lqhwua_post
ae_int16x4 la16x4_ric_3arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA16X4_RIC(d, al, p);
  return d;
}

// IR-LABEL: @la32x2_ric_4arg
// Reverse UA 64b load: dir ImmArg = 1 (not forward 0).
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// O0-LABEL: @la32x2_ric_4arg
// O0: call {{.*}}@haydn_ae_la64_step({{.*}}i32 noundef 8, i32 noundef 1)
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef -8
// ASM-LABEL: la32x2_ric_4arg
// ASM: d_ltwua_post
ae_int32x2 la32x2_ric_4arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIC(d, al, p, 0);
  return d;
}

// IR-LABEL: @la32x2_ric_3arg
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// ASM-LABEL: la32x2_ric_3arg
// ASM: d_ltwua_post
ae_int32x2 la32x2_ric_3arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIC(d, al, p);
  return d;
}
