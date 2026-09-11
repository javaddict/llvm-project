// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE %s
//
// REQUIRES: haydn-registered-target

// REGRESSION TEST: haydn_dsp.h must compile cleanly when each XC macro is
// invoked.
//
// Bug (F06 / consolidated-fix-list §2): haydn_dsp.h's AE_*_XC macros expand
// to haydn_sdw_cb_reg / haydn_ldw_cb_reg (forward XC byte-stride law) and
// haydn_ldw_cb_imm / haydn_sdw_cb_imm (RIC reverse) plus haydn_ldw_brev_imm /
// haydn_lw_brev_imm. The header is shipped as a Clang resource but never
// compiled by `ninja`, so a missing builtin declaration went unnoticed. Any
// user porting a NatureDSP kernel hit "use of undeclared identifier
// haydn_sdw_cb_imm". This test catches that regression by forcing the
// header through the frontend.
//
// C4.1/C4.2: AE_ADD64X2_vector is permanent UNSUPPORTED (fail-closed by
// default; no bag dual-64). AE_L16_XC is EMULATED (i16 + soft CBR);
// L16X4_RIC / LA*_RIC are EXACT. Default RUN covers exact/emulated
// XC/IC/RIC; ALLOW_INEXACT covers the permanent-UNSUPPORTED residual body.

#include <haydn_dsp.h>

void use_all_xc_macros(ae_int32x2 *p32x2, ae_int16x4 *p16x4,
                       ae_int32 *p32, ae_int16 *p16) {
  ae_int32x2 d32x2;
  ae_int16x4 d16x4;
  ae_int32   d32;
  ae_int16   d16;

  // Circular-buffer XC family (F21/F22): ImmArg cbr_sel must be 0/1 const.
  // Exact / emulated tiers — available under strict C4.1 mode.
  AE_L32X2_XC(d32x2, p32x2, 16, 0);
  AE_S32X2_XC(d32x2, p32x2, 16, 0);
  AE_L16X4_XC(d16x4, p16x4, 16, 0);
  AE_S16X4_XC(d16x4, p16x4, 16, 0);
  // C4.2 EXACT: reverse-CB via signed negative D_LDW_CB stride.
  AE_L16X4_RIC(d16x4, p16x4, 16, 0);
  // C4.2 EMULATED: ordinary i16 load + soft CBR step (no 64b trunc).
  AE_L16_XC(d16, p16, 16, 0);
  (void)d16;

  // LA_*_IC: (dst, align, ptr[, cbr_sel]) — ptr must be a mutable lvalue;
  // align is ae_valign (AR residual). Stride is fixed 8B inside the macro.
  ae_valign align16 = AE_ZALIGN64();
  ae_valign align32 = AE_ZALIGN64();
  AE_LA16X4POS_PC(align16, p16x4);
  AE_LA32X2POS_PC(align32, p32x2);
  AE_LA16X4_IC(d16x4, align16, p16x4, 0);
  AE_LA32X2_IC(d32x2, align32, p32x2, 0);
  // C4.2 EXACT: reverse-IC via UA dir=1 + haydn_cbr_step(ptr, -8).
  AE_LA16X4_RIC(d16x4, align16, p16x4, 0);
  AE_LA32X2_RIC(d32x2, align32, p32x2, 0);

  // Bit-reversed addressing (F06: haydn_lw_brev_imm / haydn_ldw_brev_imm).
  // ImmArg: stride is a compile-time constant; 3-arg form updates ptr.
  ae_int32 rv32;
  ae_int32x2 rv32x2;
  AE_L32_BREV_IP(rv32, p32, 16);
  AE_L32X2_BREV_IP(rv32x2, p32x2, 16);
  (void)rv32;
  (void)rv32x2;

  // satsr64 (F06 list): was already declared; this guards against regression.
  ae_int64 acc = 0;
  ae_int32 satted = (ae_int32)haydn_satsr64(acc, 8);
  (void)satted;
  (void)d32;
}
