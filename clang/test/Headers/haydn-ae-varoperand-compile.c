// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -Werror -pedantic -Wno-variadic-macro-arguments-omitted %s
//
// REQUIRES: haydn-registered-target
//
// The -Werror -pedantic arm suppresses -Wvariadic-macro-arguments-omitted:
// the header's 3-arg/4-arg overload idiom (__AE_*_GET(_1.._4, NAME, ...))
// leaves the trailing ... empty for 4-arg invocations — a C23 extension
// that is a pre-existing property of every AE_*XC overload in haydn_dsp.h,
// not something this test owns.
//
// REGRESSION TEST (2026-09-02 three-bug wave): public AE_* macros must
// compile under ADVERSARIAL operand shapes, not only the benign literal
// shapes the historical corpus used. Prior corpus state: every XC macro
// expansion passed a literal ICE stride (haydn_dsp.c, haydn_dsp_xc.c 32-
// byte literals, tier-taxonomy 16s), so the imm-only haydn_ldw_cb_imm
// routing — a hard Sema error on any variable stride — was invisible;
// AE_SHORTSWAP and AE_MOVAD16_* had ZERO expansions in all of clang/test/
// and shipped bodies that were hard errors on ANY use.
//
// Laws pinned here (compile layer; IR layer: haydn-ae-varoperand-ir.c):
//   1. AE_{L,S}{16X4,32X2,32X2F24}_XC accept a RUNTIME variable byte
//      stride in the 3-arg HiFi form and the 4-arg explicit-cbr_sel form
//      (cbr_sel stays an ICE ImmArg). Variable stride routes through
//      D_LDW_CB_REG / D_SDW_CB_REG (golden reg twins; reg stride is
//      RAW BYTES, no imm<<3 scaling).
//   2. AE_SHORTSWAP(a) is a legal haydn_x4seli16 call site: golden
//      sel=0 is the 16-bit halves swap (Xtensa AE_SHORTSWAP peer).
//      imm outside uimm4 [0,15] is a Sema error on every use.
//   3. AE_MOVAD16_0..3(a) are lane extracts from ae_int16x4 yielding an
//      ae_int16 SCALAR. NatureDSP bexp idiom consumes them as ints:
//      i = AE_MOVAD16_0(acc); shift = NSA(i) - 16; in-tree NSA peer is
//      XT_NSA. Vector<->int arithmetic in the body is a type error on
//      every use.

#include <haydn_dsp.h>

/* ---- Law 1: runtime-variable byte stride through the XC family ---- */
void xc_macros_variable_stride(ae_int16x4 *p16x4, ae_int32x2 *p32x2,
                               ae_f24x2 *p24, int stride) {
  ae_int16x4 d16x4 = {0};
  ae_int32x2 d32x2 = {0};
  ae_f24x2 d24 = 0;

  /* HiFi 3-arg forms (implicit CBR0) with a runtime stride — the shape
   * every real kernel uses (fft_cplx16x16_hifi3.c:950). */
  AE_L16X4_XC(d16x4, p16x4, stride);
  AE_S16X4_XC(d16x4, p16x4, stride);
  AE_L32X2_XC(d32x2, p32x2, stride);
  AE_S32X2_XC(d32x2, p32x2, stride);
  AE_L32X2F24_XC(d24, p24, stride);
  AE_S32X2F24_XC(d24, p24, stride);

  /* 4-arg explicit-cbr_sel forms: cbr_sel ICE 0/1, stride stays a runtime
   * GPR value — including foldable-LOOKING non-ICE expressions. */
  AE_L16X4_XC(d16x4, p16x4, (stride << 1) & ~7, 0);
  AE_S16X4_XC(d16x4, p16x4, stride + 8, 1);
  AE_L32X2_XC(d32x2, p32x2, stride * 2, 0);
  AE_S32X2_XC(d32x2, p32x2, stride, 1);
  AE_L32X2F24_XC(d24, p24, stride, 1);
  AE_S32X2F24_XC(d24, p24, stride, 0);

  /* Historical ICE call sites keep compiling (single reg law must not
   * regress the constant-stride arms pinned by haydn_dsp_xc.c). */
  AE_L16X4_XC(d16x4, p16x4, 16, 0);
  AE_L16X4_XC(d16x4, p16x4, 16);

  (void)d16x4;
  (void)d32x2;
  (void)d24;
}

/* ---- Law 2: AE_SHORTSWAP is a legal x4seli16 call site ---- */
ae_int16x4 shortswap_roundtrip(ae_int16x4 a) {
  ae_int16x4 s = AE_SHORTSWAP(a);
  s = AE_SHORTSWAP(s); /* halves swap is an involution */
  return s;
}

/* ---- Law 3: AE_MOVAD16_* lane extracts (bexp idiom) ---- */
/* Peer: hifi vec_max/fft bexp loops consume
 *   i = AE_MOVAD16_0(acc); bexp = NSA(i) - 16;
 * where NSA counts sign bits. In-tree NSA spelling is XT_NSA. */
int movad16_bexp_idiom(ae_int16x4 acc) {
  int bexp = 0;
  int i;

  i = AE_MOVAD16_0(acc);
  bexp = XT_NSA(i) - 16;
  i = AE_MOVAD16_1(acc);
  bexp += XT_NSA(i);
  i = AE_MOVAD16_2(acc);
  bexp += XT_NSA(i);
  i = AE_MOVAD16_3(acc);
  bexp += XT_NSA(i);
  return bexp;
}

ae_int16 movad16_scalar_lanes(ae_int16x4 a) {
  ae_int16 l0 = AE_MOVAD16_0(a);
  ae_int16 l1 = AE_MOVAD16_1(a);
  ae_int16 l2 = AE_MOVAD16_2(a);
  ae_int16 l3 = AE_MOVAD16_3(a);
  return (ae_int16)(l0 + l1 + l2 + l3);
}
