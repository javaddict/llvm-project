// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only %s
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -I %S/../../../../utils/haydn/freestanding-shims \
// RUN:   -DTEST_NDSP_OVERLAY -fsyntax-only %s
//
// __AE_TO_I64 must accept official GNU haydn_x2int32 and overlay
// haydn_ndsp_i32x2 in one TU. Those are compatible <2 x i32> types;
// listing both in one _Generic is a Clang association error and blocks
// NatureDSP vec_add16/vec_add32/vec_dot16 compile.
// Overlay sites: ae_int32x2 v = 0, dest-typed AE_MULAF16X4SS,
// AE_ROUND16X4F32SASYM(0, v), AE_L16X2M_IU.

#include <haydn_dsp.h>
#ifdef TEST_NDSP_OVERLAY
#include "ndsp_ae_overlay.h"
#endif

int64_t bag_gnu(haydn_x2int32 a) { return __AE_TO_I64(a); }
int64_t bag_ndsp(haydn_ndsp_i32x2 a) { return __AE_TO_I64(a); }
int64_t bag_i64(int64_t a) { return __AE_TO_I64(a); }

#ifdef TEST_NDSP_OVERLAY
ae_int16x4 ndsp_overlay_sites(ae_int32 *p, ae_f32x2 acc_hi, ae_f32x2 acc_lo,
                              ae_int16x4 a, ae_int16x4 b, ae_f32x2 *out_hi,
                              ae_f32x2 *out_lo) {
  ae_int32x2 z = 0;
  ae_f32x2 f = 0;
  ae_int32x2 x;
  AE_L16X2M_IU(x, p, 4);
  AE_MULAF16X4SS(acc_hi, acc_lo, a, b);
  *out_hi = acc_hi;
  *out_lo = acc_lo;
  (void)f;
  return AE_ROUND16X4F32SASYM(0, z) + AE_ROUND16X4F32SASYM(x, z);
}

ae_int32x2 ndsp_vec_add32(ae_int32x2 a, ae_int32x2 b) { return AE_ADD32(a, b); }
ae_int16x4 ndsp_vec_add16(ae_int16x4 a, ae_int16x4 b) { return AE_ADD16(a, b); }
#endif
