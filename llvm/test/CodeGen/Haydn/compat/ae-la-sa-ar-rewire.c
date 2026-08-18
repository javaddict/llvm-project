// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -mllvm -stop-after=instruction-select -S -o - %s | FileCheck %s

// Role: semantic — E2E: NatureDSP AE_LA*/AE_SA* compat macros must lower to Haydn AR unaligned ops (PLDWWUA / D_L*UA_POST / D_S*UA_POST / WBARWUA / FLAR).

// E2E: NatureDSP AE_LA*/AE_SA* compat macros must lower to Haydn AR
// unaligned ops (PLDWWUA / D_L*UA_POST / D_S*UA_POST / WBARWUA / FLAR)
// not plain loads/stores.
//
// Full model (-mcpu=haydn): simd + bit-reversed + CB gates so haydn.h parses.
// Default CPU generic is agu+hwloop only and fails feature-gated builtins.

#include <haydn_dsp.h>

// CHECK-LABEL: name: la32x2_stream
// CHECK: PLDWWUA
// CHECK: {{D_LTWUA_POST|LTWUA}}

void la32x2_stream(const ae_int32x2 *x, ae_int32x2 *out) {
  ae_valign a = AE_LA64_PP(x);
  ae_int32x2 v0, v1;
  AE_LA32X2_IP(v0, a, x);
  AE_LA32X2_IP(v1, a, x);
  out[0] = v0;
  out[1] = v1;
}

// CHECK-LABEL: name: la16x4_stream
// CHECK: PLDWWUA
// CHECK: {{D_LQHWUA_POST|LQHWUA}}
void la16x4_stream(const ae_int16x4 *x, ae_int16x4 *out) {
  ae_valign a = AE_LA64_PP(x);
  ae_int16x4 v;
  AE_LA16X4_IP(v, a, x);
  *out = v;
}

// CHECK-LABEL: name: sa32x2_stream
// CHECK: FLAR
// CHECK: {{D_STWUA_POST|STWUA}}
// CHECK: WBARWUA
void sa32x2_stream(ae_int32x2 *y, ae_int32x2 v0, ae_int32x2 v1) {
  ae_valign a = AE_ZALIGN64();
  AE_SA32X2_IP(v0, a, y);
  AE_SA32X2_IP(v1, a, y);
  AE_SA64POS_FP(a, y);
}

// CHECK-LABEL: name: sa16x4_stream
// CHECK: FLAR
// CHECK: {{D_SQHWUA_POST|SQHWUA}}
// CHECK: WBARWUA
void sa16x4_stream(ae_int16x4 *y, ae_int16x4 v) {
  ae_valign a = AE_ZALIGN64();
  AE_SA16X4_IP(v, a, y);
  AE_SA64POS_FP(a, y);
}

// CHECK-LABEL: name: scale_like
// CHECK: PLDWWUA
// CHECK: FLAR
// CHECK: {{D_LTWUA_POST|LTWUA}}
// CHECK: {{D_STWUA_POST|STWUA}}
void scale_like(ae_int32x2 *restrict y, const ae_int32x2 *restrict x,
                ae_int32x2 s, int N) {
  ae_valign xa = AE_LA64_PP(x);
  ae_valign ya = AE_ZALIGN64();
  for (int n = 0; n < N - 1; n += 2) {
    ae_int32x2 vxw, vyw;
    AE_LA32X2_IP(vxw, xa, x);
    vyw = AE_MULFP32X2RAS(vxw, s);
    AE_SA32X2_IP(vyw, ya, y);
  }
  AE_SA64POS_FP(ya, y);
}

// Dual-24 unaligned circular: POS_PC seeds AR (PLDWWUA); IC uses UA residual
// + soft CBR wrap — never plain load/store and never aligned D_*_CB alone.
// CHECK-LABEL: name: la32x2f24_ic_stream
// CHECK: PLDWWUA
// CHECK: {{D_LTWUA_POST|LTWUA}}
// CHECK-NOT: D_LDW_CB
ae_f24x2 *la32x2f24_ic_stream(ae_f24x2 *p, ae_f24x2 *out) {
  ae_valign a;
  ae_f24x2 v0, v1;
  AE_LA32X2F24POS_PC(a, p);
  AE_LA32X2F24_IC(v0, a, p, 0);
  AE_LA32X2F24_IC(v1, a, p, 0);
  out[0] = v0;
  out[1] = v1;
  return p;
}

// CHECK-LABEL: name: sa32x2f24_ic_stream
// CHECK: FLAR
// CHECK: {{D_STWUA_POST|STWUA}}
// CHECK-NOT: D_SDW_CB
ae_f24x2 *sa32x2f24_ic_stream(ae_f24x2 *p, ae_f24x2 v0, ae_f24x2 v1) {
  ae_valign a = AE_ZALIGN64();
  AE_SA32X2F24_IC(v0, a, p, 0);
  AE_SA32X2F24_IC(v1, a, p, 0);
  return p;
}

// CHECK-LABEL: name: la32x2f24_xc_stream
// CHECK: {{D_LTWUA_POST|LTWUA}}
// CHECK-NOT: D_LDW_CB
ae_f24x2 *la32x2f24_xc_stream(ae_f24x2 *p, ae_valign a, ae_f24x2 *out) {
  ae_f24x2 v;
  AE_LA32X2F24_XC(v, a, p, 8, 0);
  *out = v;
  return p;
}

// Dual load streams on distinct ARs (default LA=AR0, explicit AR1).
// Architectural AR file is AR0/AR1 only; selectors 2/3 are not product-visible.
// CHECK-LABEL: name: dual_load_streams
// CHECK: PLDWWUA
// CHECK: PLDWWUA
// CHECK: {{D_LTWUA_POST|LTWUA}}
void dual_load_streams(const ae_int32x2 *a, const ae_int32x2 *b,
                       ae_int32x2 *out) {
  ae_valign xa = AE_LA64_PP(a);
  ae_valign xb = AE_LA64_PP_AR(1, b);
  ae_int32x2 va, vb;
  AE_LA32X2_IP(va, xa, a);
  AE_LA32X2_IP(vb, xb, b);
  out[0] = va;
  out[1] = vb;
}

// SCEV-friendly: loop must keep a phi on the C cursor (next-ptr), not freeze
// on a single address. AR op supplies data; GEP is the stream index.
// CHECK-LABEL: name: la_loop_keeps_phi
// CHECK: {{D_LTWUA_POST|LTWUA}}
void la_loop_keeps_phi(const ae_int32x2 *restrict x, ae_int32x2 *restrict out,
                       int N) {
  ae_valign a = AE_LA64_PP(x);
  for (int i = 0; i < N; ++i) {
    ae_int32x2 v;
    AE_LA32X2_IP(v, a, x);
    out[i] = v;
  }
}
