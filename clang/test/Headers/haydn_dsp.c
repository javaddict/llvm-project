// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -ffreestanding %s
//
// REQUIRES: haydn-registered-target

// REGRESSION TEST: haydn_dsp.h must compile cleanly when each XC macro is
// invoked.
//
// Bug (F06 / consolidated-fix-list §2): haydn_dsp.h's AE_*_XC macros expand
// to __haydn_sdw_cb_imm / __haydn_ldw_cb_imm / __haydn_ldw_brev_imm /
// __haydn_lw_brev_imm. The header is shipped as a Clang resource but never
// compiled by `ninja`, so a missing builtin declaration went unnoticed. Any
// user porting a NatureDSP kernel hit "use of undeclared identifier
// __haydn_sdw_cb_imm". This test catches that regression by forcing the
// header through the frontend.

#include <haydn_dsp.h>

void use_all_xc_macros(ae_int32x2 *p32x2, ae_int16x4 *p16x4,
                       ae_int32 *p32, ae_int16 *p16, int cbr_sel) {
  ae_int32x2 d32x2;
  ae_int16x4 d16x4;
  ae_int32   d32;
  ae_int16   d16;

  // Circular-buffer XC family (F21/F22/F23): offs and cbr_sel are exercised.
  AE_L32X2_XC(d32x2, p32x2, 16, cbr_sel);
  AE_S32X2_XC(d32x2, p32x2, 16, cbr_sel);
  AE_L16X4_XC(d16x4, p16x4, 16, cbr_sel);
  AE_S16X4_XC(d16x4, p16x4, 16, cbr_sel);
  AE_L16_XC(d16, p16, 16, cbr_sel);

  // Reverse-increment / aligned-IC variants (also exercise __haydn_ldw_cb_imm).
  AE_L16X4_RIC(d16x4, p16x4, 16, cbr_sel);
  AE_LA16X4_IC(d16x4, p16x4, 16, cbr_sel);
  AE_LA16X4_RIC(d16x4, p16x4, 16, cbr_sel);
  AE_LA32X2_IC(d32x2, p32x2, 16, cbr_sel);

  // Bit-reversed addressing (F06: __haydn_lw_brev_imm / __haydn_ldw_brev_imm).
  ae_int32 rv32 = AE_L32_BREV_IP(p32, 16);
  ae_int32x2 rv32x2 = AE_L32X2_BREV_IP(p32x2, 16);
  (void)rv32;
  (void)rv32x2;

  // satsr64 (F06 list): was already declared; this guards against regression.
  ae_int64 acc = 0;
  ae_int32 satted = (ae_int32)__haydn_satsr64(acc, 8);
  (void)satted;
}
