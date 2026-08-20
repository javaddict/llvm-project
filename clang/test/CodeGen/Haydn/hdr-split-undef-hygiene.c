// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding \
// RUN:   -fsyntax-only %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding \
// RUN:   -E -dM %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// HDR-SPLIT measured miss: stay one haydn_dsp.h (no family-header split,
// no native FIR/FFT ISA). Hygiene pin: AE_L32_XP is defined once as an
// arity overload (no second #undef), and internal overload helpers remain
// available to that public macro.

#include <haydn_dsp.h>

#if defined(__HAYDN_DSP_FP_H) || defined(__HAYDN_DSP_HDR_SPLIT)
#error "do not split haydn_dsp.h into family headers"
#endif
#if defined(AE_FIR_NATIVE_COMPOSITE) || defined(AE_FFT_NATIVE_COMPOSITE)
#error "do not invent a native FIR/FFT composite ISA"
#endif

void hdr_l32_xp_3(ae_int32 *p) {
  ae_int32 v;
  AE_L32_XP(v, p, 4);
  (void)v;
}

void hdr_l32_xp_4(ae_int32 *p) {
  ae_int32 v;
  AE_L32_XP(v, p, 0, 4);
  (void)v;
}

// CHECK: #define AE_L32_XP
// CHECK: #define __AE_L32_XP_OVERLOAD
