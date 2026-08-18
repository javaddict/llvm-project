// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -Werror=incompatible-pointer-types-discards-qualifiers \
// RUN:   -Werror=cast-qual -fsyntax-only %s
//
// NatureDSP bkfir / vec_scale / vec_dot16 walk `const ae_int16x4 *`.
// Load IP / LA IP maps must not drop const on the cursor.

#include <haydn_dsp.h>

void bkfir_const_l16x4_ip(const ae_int16x4 *x, ae_int16x4 *out) {
  ae_int16x4 t0, t1;
  AE_L16X4_IP(t0, x, +8);
  AE_L16X4_IP(t1, x, +8);
  out[0] = t0;
  out[1] = t1;
}

void vec_scale_const_l16x4_ip(const ae_int16x4 *restrict px, ae_int16x4 *out) {
  ae_int16x4 vxh;
  AE_L16X4_IP(vxh, px, sizeof(*px));
  *out = vxh;
}

void vec_dot16_const_pair(const ae_int16x4 *restrict px,
                          const ae_int16x4 *restrict py, ae_int16x4 *ox,
                          ae_int16x4 *oy) {
  ae_int16x4 vxh, vyh;
  AE_L16X4_IP(vxh, px, sizeof(*px));
  AE_L16X4_IP(vyh, py, sizeof(*py));
  *ox = vxh;
  *oy = vyh;
}

void la16x4_const_stream(const ae_int16x4 *x, ae_int16x4 *out) {
  ae_valign a = AE_LA64_PP(x);
  ae_int16x4 v;
  AE_LA16X4_IP(v, a, x);
  *out = v;
}
