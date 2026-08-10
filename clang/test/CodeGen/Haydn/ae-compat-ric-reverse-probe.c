// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
//
// REQUIRES: haydn-registered-target
//
// Residual reverse-circular family: RIC must use reverse path (dir ImmArg=1
// on UA step, or negative D_LDW_CB element stride). Must not silent-alias
// forward IC (dir=0 / +stride). NEG_PC remains probe-only POS seed.

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_RIC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_RIC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIC == HAYDN_COMPAT_EXACT, "");

// IR-LABEL: @l32x2_ric_reverse_cb
// Reverse aligned CB: negative D_LDW_CB element stride (i32 -1 for offs=8).
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm({{.*}}i32 -1)
void l32x2_ric_reverse_cb(ae_int32x2 *dst, ae_int32x2 *ptr) {
  AE_L32X2_RIC(*dst, ptr, 8, 0);
}

// IR-LABEL: @l16x4_ric_reverse_cb
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm({{.*}}i32 -1)
void l16x4_ric_reverse_cb(ae_int16x4 *dst, ae_int16x4 *ptr) {
  AE_L16X4_RIC(*dst, ptr, 8, 0);
}

// IR-LABEL: @la32x2_ric_reverse_ua
// Reverse unaligned: UA post last ImmArg is dir=1 (not forward IC dir=0).
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr{{.*}}, i32 {{.*}}, i32 8, i32 1)
void la32x2_ric_reverse_ua(ae_int32x2 *dst, ae_valign *al, ae_int32x2 *ptr) {
  AE_LA32X2_RIC(*dst, *al, ptr, 0);
}

// IR-LABEL: @la16x4_ric_reverse_ua
// IR: call {{.*}}@llvm.haydn.d.lqhwua.post(ptr{{.*}}, i32 {{.*}}, i32 8, i32 1)
void la16x4_ric_reverse_ua(ae_int16x4 *dst, ae_valign *al, ae_int16x4 *ptr) {
  AE_LA16X4_RIC(*dst, *al, ptr, 0);
}

// IR-LABEL: @l32x2f24_ric_reverse_cb
// IR: call {{.*}}@llvm.haydn.ldw.cb.imm({{.*}}i32 -1)
void l32x2f24_ric_reverse_cb(ae_f24x2 *dst, ae_f24x2 *ptr) {
  AE_L32X2F24_RIC(*dst, ptr, 8, 0);
}

// IR-LABEL: @la32x2f24_ric_reverse_ua
// Reverse unaligned F24: UA post dir=1 + CBR step -8 (not forward IC).
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr{{.*}}, i32 {{.*}}, i32 8, i32 1)
void la32x2f24_ric_reverse_ua(ae_f24x2 *dst, ae_valign *al, ae_f24x2 *ptr) {
  AE_LA32X2F24_RIC(*dst, *al, ptr, 0);
}
