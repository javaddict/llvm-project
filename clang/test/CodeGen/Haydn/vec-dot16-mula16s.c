// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 \
// RUN:   -ffreestanding -emit-llvm -o - %s | FileCheck %s
// REQUIRES: haydn-registered-target
//
// NatureDSP vec_dot16 host: dest-typed AE_MULAF16X4SS is X4MULA16S, not
// FMULAA16. Integer host of
//   x=[10,-10,20,-20,30,40,50,60] y=[1,2,3,4,5,6,7,8]
// is 1190; the two-lane HS_11_00 body is 380. Public X2CMUL wrappers stay
// fail-closed under default strict.

#include <haydn_dsp.h>

// CHECK-LABEL: @vec_dot16_sat_mac
// CHECK: call { i64, i64 } @llvm.haydn.x4mula16s
// CHECK-NOT: fmul
void vec_dot16_sat_mac(ae_f32x2 *vaf, ae_f32x2 *vbf,
                       ae_int16x4 x, ae_int16x4 y) {
  AE_MULAF16X4SS(*vaf, *vbf, x, y);
}
