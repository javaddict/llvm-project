// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s | FileCheck %s
//
// REQUIRES: haydn-registered-target
//
// HiFi contract: AE_ADDANDSUBRNG16RAS_S1(a, b) is a *statement* that updates
// both a and b (a:=a+b, b:=a-b with sat). A discarded pure return leaves FFT
// DFT4XI2 without butterfly arithmetic (P8).
//
// CHECK-LABEL: @dual_update
// CHECK: call {{.*}}@llvm.haydn.x4add16s
// CHECK: call {{.*}}@llvm.haydn.x4sub16s
// CHECK: store
// CHECK: store

#include <haydn_dsp.h>

void dual_update(ae_int16x4 *pa, ae_int16x4 *pb) {
  ae_int16x4 a = *pa;
  ae_int16x4 b = *pb;
  AE_ADDANDSUBRNG16RAS_S1(a, b);
  *pa = a;
  *pb = b;
}
