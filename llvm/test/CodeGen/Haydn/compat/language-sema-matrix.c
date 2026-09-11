// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only %s
// RUN: clang -target haydn-unknown-elf -ffreestanding -fsyntax-only %s
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_X2CMUL_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CMUL
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_I128 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=I128
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_FLOAT16 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=F16
//
// G-LANGUAGE-COVERAGE Sema matrix (compat-owned). Advertised haydn_dsp.h
// surface compiles at empty/-mcpu=generic/-mcpu=haydn (same full ISA).
// AE_CMUL32_F2 stays SOURCE-REVALIDATE / fail-closed. AE_MULC32X16_* is
// EXACT. Folded MAP-FP / COMPOSITE / HDR-SPLIT stay one header. Empty
// output fails.

#include <haydn_dsp.h>

_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
#ifndef AE_MULAAAAQ16
_Static_assert(0, "AE_MULAAAAQ16 must be defined unconditionally");
#endif
#if defined(AE_FIR_NATIVE_COMPOSITE) || defined(AE_FFT_NATIVE_COMPOSITE)
_Static_assert(0, "do not invent a native FIR/FFT composite ISA");
#endif
#if defined(__HAYDN_DSP_FP_H) || defined(__HAYDN_DSP_HDR_SPLIT)
_Static_assert(0, "do not split haydn_dsp.h into family headers");
#endif
#if defined(__HAYDN_ACC_GUARD_BITS)
_Static_assert(0, "do not invent extra HiFi accumulator guard bits");
#endif

#ifndef TEST_X2CMUL_STRICT
#ifndef TEST_I128
#ifndef TEST_FLOAT16
int sema_add16(ae_int16x4 a, ae_int16x4 b) {
  return (int)AE_ADD16(a, b)[0];
}

int32_t sema_vec_dot16_host(void) {
  return haydn_satsr64((int64_t)1 * (int64_t)2, 0);
}

int64_t sema_srai64r_id(void) { return haydn_ae_asr_round64_host(7, 0); }

int64_t sema_dest_typed_assign(void) {
  ae_f32x2 v;
  __AE_ASSIGN_BITS(v, 0x0000000200000001LL);
  return __AE_TO_I64(v);
}

ae_int64 sema_mulaaaaq16(ae_int64 acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULAAAAQ16(acc, a, b);
  return acc;
}
#endif
#endif
#endif

#ifdef TEST_X2CMUL_STRICT
// CMUL: silent-wrong map removed): AE_CMUL32_F2
void sema_cmul32_f2(ae_int32x2 *d0, ae_int32x2 *d1, ae_int32x2 a,
                    ae_int32x2 b) {
  AE_CMUL32_F2(*d0, *d1, a, b);
}
#endif

#ifdef TEST_I128
// I128: {{__int128|_BitInt|not supported|unsupported}}
__int128 sema_i128_reject(__int128 x) { return x; }
#endif

#ifdef TEST_FLOAT16
// F16: {{_Float16|__fp16|not supported|unsupported}}
_Float16 sema_f16_reject(_Float16 x) { return x; }
#endif
