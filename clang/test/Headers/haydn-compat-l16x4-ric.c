// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// C4.2 / G-DSP-COMPAT: AE_L16X4_RIC is EXACT — reverse circular load via
// haydn_ldw_cb_imm with signed negative stride -((inc)>>3). Must not silently
// alias forward XC (positive stride). Available under default fail-closed mode
// (no __HAYDN_ALLOW_INEXACT_AE required). Peer path: D_LDW_CB ImmCheckSimm8.
//
// Host oracle: byte inc → element stride = -((inc)>>3); post-inc by imm<<3
// with CBR wrap below BEGIN. inc=16 → -2; inc=8 → -1. Forward XC contrast
// is +((inc)>>3). Full value/ref suite: haydn-compat-exact-value-ref.c

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_RIC == HAYDN_COMPAT_EXACT,
               "L16X4_RIC is exact reverse-CB via negative D_LDW_CB stride (C4.2)");

// IR-LABEL: @l16x4_ric_4arg
// Reverse 16-byte step → element stride -2 (imm<<3 = -16).
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -2
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 2
// ASM-LABEL: l16x4_ric_4arg
// ASM: d_ldw_cb_imm
ae_int16x4 *l16x4_ric_4arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_RIC(d, p, 16, 0);
  (void)d;
  return p;
}

// IR-LABEL: @l16x4_ric_3arg
// Default cbr_sel=0; reverse 8-byte step → element stride -1.
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -1
// ASM-LABEL: l16x4_ric_3arg
// ASM: d_ldw_cb_imm
ae_int16x4 *l16x4_ric_3arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_RIC(d, p, 8);
  (void)d;
  return p;
}

// IR-LABEL: @l16x4_ric_2arg
// Default offs=8, cbr_sel=0 → stride -1 (not forward +1).
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 -1
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm{{.*}}i32 0, i32 1
// ASM-LABEL: l16x4_ric_2arg
// ASM: d_ldw_cb_imm
ae_int16x4 *l16x4_ric_2arg(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_RIC(d, p);
  (void)d;
  return p;
}

// Direction contrast: forward XC keeps the reg path with positive byte
// stride (silent RIC→XC would share the imm path).
// IR-LABEL: @l16x4_xc_pos_contrast
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg
// ASM-LABEL: l16x4_xc_pos_contrast
// ASM: d_ldw_cb_reg
ae_int16x4 *l16x4_xc_pos_contrast(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_XC(d, p, 16, 0);
  (void)d;
  return p;
}
