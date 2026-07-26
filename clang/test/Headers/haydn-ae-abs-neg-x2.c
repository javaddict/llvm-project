// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s | FileCheck %s
//
// REQUIRES: haydn-registered-target
//
// Contract: AE_ABS32S / AE_NEG32S on ae_int32x2 must lower to dual-lane
// X2ABS32S / X2NEG32S, not scalar abs32s/neg32s (which destroy the high lane).
//
// CHECK-LABEL: @abs32s_vec
// CHECK: call {{.*}}@llvm.haydn.x2abs32s
// CHECK-NOT: call {{.*}}@llvm.haydn.abs32s
//
// CHECK-LABEL: @neg32s_vec
// CHECK: call {{.*}}@llvm.haydn.x2neg32s
// CHECK-NOT: call {{.*}}@llvm.haydn.neg32s
//
// CHECK-LABEL: @abs32_vec
// CHECK: call {{.*}}@llvm.haydn.x2abs32
//
// CHECK-LABEL: @neg32_vec
// CHECK: call {{.*}}@llvm.haydn.x2neg32

#include <haydn_dsp.h>

ae_int32x2 abs32s_vec(ae_int32x2 x) { return AE_ABS32S(x); }
ae_int32x2 neg32s_vec(ae_int32x2 x) { return AE_NEG32S(x); }
ae_int32x2 abs32_vec(ae_int32x2 x) { return AE_ABS32(x); }
ae_int32x2 neg32_vec(ae_int32x2 x) { return AE_NEG32(x); }
