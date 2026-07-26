// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
//
// REQUIRES: haydn-registered-target
//
// C4.2 / G-DSP-COMPAT: AE_L16_XC is EMULATED — ordinary i16 load + soft CBR
// step via haydn_cbr_step(byte offs, cbr_sel). Must not use D_LDW_CB 64b load
// + trunc or offs>>3 element units (F23 silent-wrong). Available under default
// fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE). Hexagon peer is halfword
// circular load (L2_loadrh_pci); Haydn has no S_LH_CB (ISA-14).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_L16_XC == HAYDN_COMPAT_EMULATED,
               "L16_XC is emulated i16 load + soft CBR step (C4.2)");

// Module-wide: never lower through 64-bit circular load.
// IR-NOT: @llvm.haydn.ldw.cb.imm
// O0-NOT: @llvm.haydn.ldw.cb.imm

// O0-LABEL: define {{.*}} @l16_xc_4arg(
// Ordinary i16 load + soft byte-stride CBR step (offs=16, not offs>>3 → 2).
// O0: load i16
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef 16
//
// IR-LABEL: define {{.*}} @l16_xc_4arg(
// Return keeps the i16 load live at O2; soft step may fold away.
// IR: load i16
ae_int16 l16_xc_4arg(ae_int16 *p) {
  ae_int16 d = 0;
  AE_L16_XC(d, p, 16, 0);
  return d;
}

// O0-LABEL: define {{.*}} @l16_xc_3arg(
// Default cbr_sel=0; byte offs=32 still soft-stepped.
// O0: load i16
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef 32
//
// IR-LABEL: define {{.*}} @l16_xc_3arg(
// IR: load i16
ae_int16 l16_xc_3arg(ae_int16 *p) {
  ae_int16 d = 0;
  AE_L16_XC(d, p, 32);
  return d;
}

// O0-LABEL: define {{.*}} @l16_xc_cbr_sel1(
// cbr_sel=1 selects CBR set 1 mirrors; still soft step, never D_LDW_CB.
// O0: load i16
// O0: call {{.*}}@haydn_cbr_step({{.*}}i32 noundef 8, i32 noundef 1
//
// IR-LABEL: define {{.*}} @l16_xc_cbr_sel1(
// IR: load i16
ae_int16 l16_xc_cbr_sel1(ae_int16 *p) {
  ae_int16 d = 0;
  AE_L16_XC(d, p, 8, 1);
  return d;
}
