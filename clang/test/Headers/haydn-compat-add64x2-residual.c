// RUN: not %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s 2>&1 | FileCheck %s --check-prefix=STRICT
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -D__HAYDN_ALLOW_INEXACT_AE %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -D__HAYDN_ALLOW_INEXACT_AE -o - %s \
// RUN:   | FileCheck %s --check-prefix=INEXACT
//
// REQUIRES: haydn-registered-target
//
// NatureDSP AE compatibility: AE_ADD64X2_vector and AE_ADD64X2_ are permanent
// UNSUPPORTED. HiFi dual-64 lane-wise add (no cross-lane carry) has no
// Haydn map: ae_int64x2 is a single DR64 bag; a scalar i64 add is
// silent-wrong (cross-lane carry). Plain AE_ADD64X2_ is the same class
// under a different name. Do not EMULATE or bag-alias dual-64. Default
// mode fail-closes; __HAYDN_ALLOW_INEXACT_AE keeps the transitional
// scalar body for NatureDSP -c only. Hexagon peer is fail-closed /
// feature-gated C surface, not a silent scalar alias. Research:
// ae-to-haydn-mapping AE_ADD64X2_vector MISSING (128-bit dual-64).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2 permanent UNSUPPORTED (no bag dual-64; C4.2)");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2_ permanent UNSUPPORTED (same dual-64 class)");

#if !defined(__HAYDN_ALLOW_INEXACT_AE)
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_vector
ae_int64x2 use_add64x2_strict(ae_int64x2 a, ae_int64x2 b) {
  return AE_ADD64X2_vector(a, b);
}
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_
ae_int64 use_add64x2_plain_strict(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_(a, b);
}
#else
// Transitional NatureDSP -c: inexact scalar i64 add is intentionally
// available (knowingly wrong carry). Must not claim EXACT/EMULATED tier.
// INEXACT-LABEL: @use_add64x2_inexact
// INEXACT: add {{.*}}i64
// INEXACT-NOT: __haydn_ae_unsupported
ae_int64x2 use_add64x2_inexact(ae_int64x2 a, ae_int64x2 b) {
  return AE_ADD64X2_vector(a, b);
}
// INEXACT-LABEL: @use_add64x2_plain_inexact
// INEXACT: add {{.*}}i64
// INEXACT-NOT: __haydn_ae_unsupported
ae_int64 use_add64x2_plain_inexact(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_(a, b);
}
#endif
