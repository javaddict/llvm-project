// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: not %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -DTEST_ADD64X2_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STRICT
//
// REQUIRES: haydn-registered-target
//
// Product-law residual: AE_MAXABS16S is EMULATED via x4abs16s+x4max16
// (never maxabs32s). AE_ADD64X2_ / _vector stay UNSUPPORTED under default
// fail-closed mode.

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_MAXABS16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED, "");

// IR-LABEL: @use_maxabs16s
// IR: call {{.*}}@llvm.haydn.x4abs16s
// IR: call {{.*}}@llvm.haydn.x4max16
// IR-NOT: maxabs32
ae_int16x4 use_maxabs16s(ae_int16x4 acc, ae_int16x4 v) {
  return AE_MAXABS16S(acc, v);
}

// IR-LABEL: @use_maxabs16s_1arg
// IR: call {{.*}}@llvm.haydn.x4abs16s
// IR-NOT: maxabs32
ae_int16x4 use_maxabs16s_1arg(ae_int16x4 v) {
  return AE_MAXABS16S(v);
}

#if defined(TEST_ADD64X2_STRICT)
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_
ae_int64 use_add64x2_plain(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_(a, b);
}
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_vector
ae_int64x2 use_add64x2_vector(ae_int64x2 a, ae_int64x2 b) {
  return AE_ADD64X2_vector(a, b);
}
#endif
