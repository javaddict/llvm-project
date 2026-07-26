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
//        (haydn_sdw_cb_imm(int64_t data, int ptr, int cbr_sel, int offs)).
//   F23: AE_L16_XC used D_LDW_CB 64b trunc + offs>>3; C4.2 repairs it as
//        EMULATED ordinary i16 load + haydn_cbr_step(byte offs) under default
//        strict mode (no ALLOW_INEXACT required).
//
// Test design: this is a compile-only test that exercises each XC macro with
// a non-default `offs` (32 bytes). If the macros regress to the literal 8
// or to the wrong arg order, the frontend will still compile, but a sibling
// FileCheck-based codegen test (cb-brev-mem.c / haydn-compat-l16-xc.c)
// verifies the IR shape. This test specifically guards against
// missing-identifier regressions (F06) and arity/argument-order regressions.

#include <haydn_dsp.h>

// Use distinct `offs` values per call so a future diagnostic-test can detect
// silent literal substitution. Exact XC family offsets are multiples of 8
// (D_LDW_CB_IMM / D_SDW_CB_IMM post-increment by imm<<3). L16_XC emulated
// path accepts any byte stride.
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
