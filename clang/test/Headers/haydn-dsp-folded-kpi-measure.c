// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -ffreestanding %s
// RUN: python3 %S/haydn-dsp-folded-kpi-measure.py \
// RUN:   %S/../../lib/Headers/haydn_dsp.h
//
// REQUIRES: haydn-registered-target
//
// Folded public-header KPI miss: float AE families and FIR/FFT helpers
// stay in the one haydn_dsp.h (peer split is AIE aie2pintrin.h). No
// family-header split and no native composite ISA. Empty / generic /
// haydn are the same full ISA.

#include <haydn_dsp.h>

#if defined(AE_FIR_NATIVE_COMPOSITE) || defined(AE_FFT_NATIVE_COMPOSITE)
_Static_assert(0, "do not invent a native FIR/FFT composite ISA");
#endif
#if defined(__HAYDN_DSP_FP_H) || defined(__HAYDN_DSP_HDR_SPLIT)
_Static_assert(0, "do not split haydn_dsp.h into family headers");
#endif
#ifndef AE_MULAAAAQ16
_Static_assert(0, "AE_MULAAAAQ16 must be defined unconditionally");
#endif
#if defined(__HAYDN_ACC_GUARD_BITS)
_Static_assert(0, "do not invent extra HiFi accumulator guard bits");
#endif

int folded_kpi_pin(ae_int16x4 a, ae_int16x4 b) {
  return (int)AE_ADD16(a, b)[0];
}
