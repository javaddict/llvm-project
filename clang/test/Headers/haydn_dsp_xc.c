// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE %s
//
// REQUIRES: haydn-registered-target

// REGRESSION TEST: AE_*_XC macro argument plumbing (F21/F22/F23).
//
// Bugs (consolidated-fix-list §2):
//   F21: AE_L32X2_XC / AE_S32X2_XC / AE_L16X4_XC / AE_S16X4_XC silently
//        replaced the user-provided `offs` with the literal 8, so any kernel
//        with a non-8-byte stride corrupted its circular-buffer pointer.
//   F22: AE_S32X2_XC / AE_S16X4_XC passed arguments as (ptr, data, ...) but
//        the underlying builtin is declared data-first
//        (haydn_sdw_cb_reg(int64_t data, int ptr, int cbr_sel, int offs)).
//   F23: AE_L16_XC used D_LDW_CB 64b trunc + offs>>3; C4.2 repairs it as
//        EMULATED ordinary i16 load + haydn_cbr_step(byte offs) under default
//        strict mode (no ALLOW_INEXACT required).
//
// XC stride law: the four forward XC macros lower via D_LDW_CB_REG /
// D_SDW_CB_REG (rs2 raw BYTE stride) — one law for constant and runtime
// strides. A runtime `offs` must pass Sema (only cbr_sel stays ImmArg);
// the prior imm-only lowering rejected every variable-stride kernel with
// "argument must be a constant integer".
//
// Test design: this is a compile-only test that exercises each XC macro with
// a non-default `offs` (32 bytes) and, in the _varstride function, a
// RUNTIME offs parameter. If the macros regress to the literal 8, the imm
// form, or the wrong arg order, the frontend will still compile constant
// strides, but the runtime-stride function fails Sema under an imm-only
// regression. A sibling FileCheck-based codegen test
// (haydn-compat-l16x4-ric.c) verifies the IR shape. This test specifically
// guards against missing-identifier regressions (F06) and
// arity/argument-order regressions.

#include <haydn_dsp.h>

// Use distinct `offs` values per call so a future diagnostic-test can detect
// silent literal substitution. Exact XC family takes the byte stride raw in
// the reg form (D_LDW_CB_REG / D_SDW_CB_REG; multiples of 8 by alignment).
// L16_XC emulated path accepts any byte stride.
void exercise_xc_macros(ae_int32x2 *p32x2, ae_int16x4 *p16x4,
                        ae_int16 *p16) {
  ae_int32x2 d32x2 = {0};
  ae_int16x4 d16x4 = {0};
  ae_int16   d16 = 0;

  // ImmArg: cbr_sel must be a constant 0/1 (ISA encodes 1-bit select).
  AE_L32X2_XC(d32x2, p32x2, 32, 0);   // F21: offs must reach the builtin
  AE_S32X2_XC(d32x2, p32x2, 32, 0);   // F21 + F22: data-first, offs passed
  AE_L16X4_XC(d16x4, p16x4, 32, 0);   // F21
  AE_S16X4_XC(d16x4, p16x4, 32, 0);   // F21 + F22
  // C4.2 EMULATED: i16 + soft CBR; byte offs 32 (not offs>>3).
  AE_L16_XC(d16, p16, 32, 0);
  (void)d16;
}

// Runtime stride: real HiFi kernels (fft_cplx16x16 inner DFT4 passes a
// runtime `stride`) must compile. Only cbr_sel is ImmArg in the reg form,
// so a variable offs passes Sema.
void exercise_xc_macros_varstride(ae_int32x2 *p32x2, ae_int16x4 *p16x4,
                                  int offs) {
  ae_int32x2 d32x2 = {0};
  ae_int16x4 d16x4 = {0};

  AE_L32X2_XC(d32x2, p32x2, offs, 0);
  AE_S32X2_XC(d32x2, p32x2, offs, 0);
  AE_L16X4_XC(d16x4, p16x4, offs, 0);
  AE_S16X4_XC(d16x4, p16x4, offs, 0);
}

// F24 twins + 3-arg HiFi forms must take runtime strides too (same reg
// law; supplement to haydn-ae-varoperand-compile.c which owns the full
// adversarial-operand wave).
void exercise_xc_macros_varstride_f24(ae_f24x2 *p24, ae_int16x4 *p16x4,
                                      int offs) {
  ae_f24x2 d24 = 0;
  ae_int16x4 d16x4 = {0};

  AE_L32X2F24_XC(d24, p24, offs);   // 3-arg HiFi form, runtime stride
  AE_S32X2F24_XC(d24, p24, offs);
  AE_L32X2F24_XC(d24, p24, offs, 1); // 4-arg: ICE cbr_sel + runtime offs
  AE_S32X2F24_XC(d24, p24, offs, 1);
  AE_L16X4_XC(d16x4, p16x4, offs);   // 3-arg form (implicit CBR0)

  (void)d16x4;
}
