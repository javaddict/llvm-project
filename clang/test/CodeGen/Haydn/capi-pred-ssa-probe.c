// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O2 \
// RUN:   -mllvm -enable-post-misched=false -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target
//
// C1.1 / G-PRED-SSA exit gate (Types + public C API + IR + object):
//   haydn_pred2_t / haydn_pred4_t value compare (cmplt), mux, fused cmpsel.
//   Two independent predicate epochs consumed in reverse order must remain
//   correct pure SSA values (not ambient-SFR x2slt32/x2movt32).
//   Ambient SFR APIs stay public but are not the C1.1 fast path.

#include <haydn.h>

_Static_assert(sizeof(haydn_pred2_t) == sizeof(unsigned int), "pred2 width");
_Static_assert(sizeof(haydn_pred4_t) == sizeof(unsigned int), "pred4 width");

volatile haydn_x2int32 sink_v2;
volatile haydn_x4int16 sink_v4;
volatile haydn_pred2_t sink_p2;
volatile haydn_pred4_t sink_p4;

// IR-LABEL: @test_x2cmplt32
// IR: call i32 @llvm.haydn.x2cmplt32
haydn_pred2_t test_x2cmplt32(haydn_x2int32 a, haydn_x2int32 b) {
  haydn_pred2_t p = haydn_x2cmplt32(a, b);
  sink_p2 = p;
  return p;
}

// IR-LABEL: @test_x4cmplt16
// IR: call i32 @llvm.haydn.x4cmplt16
haydn_pred4_t test_x4cmplt16(haydn_x4int16 a, haydn_x4int16 b) {
  haydn_pred4_t p = haydn_x4cmplt16(a, b);
  sink_p4 = p;
  return p;
}

// IR-LABEL: @test_x2mux32
// IR: call <2 x i32> @llvm.haydn.x2mux32
haydn_x2int32 test_x2mux32(haydn_pred2_t p, haydn_x2int32 t, haydn_x2int32 f) {
  haydn_x2int32 r = haydn_x2mux32(p, t, f);
  sink_v2 = r;
  return r;
}

// IR-LABEL: @test_x4mux16
// IR: call <4 x i16> @llvm.haydn.x4mux16
haydn_x4int16 test_x4mux16(haydn_pred4_t p, haydn_x4int16 t, haydn_x4int16 f) {
  haydn_x4int16 r = haydn_x4mux16(p, t, f);
  sink_v4 = r;
  return r;
}

// IR-LABEL: @test_x2cmpsel32
// IR: call <2 x i32> @llvm.haydn.x2cmpsel32
haydn_x2int32 test_x2cmpsel32(haydn_x2int32 a, haydn_x2int32 b,
                              haydn_x2int32 t, haydn_x2int32 f) {
  haydn_x2int32 r = haydn_x2cmpsel32(a, b, t, f);
  sink_v2 = r;
  return r;
}

// IR-LABEL: @test_x4cmpsel16
// IR: call <4 x i16> @llvm.haydn.x4cmpsel16
haydn_x4int16 test_x4cmpsel16(haydn_x4int16 a, haydn_x4int16 b,
                              haydn_x4int16 t, haydn_x4int16 f) {
  haydn_x4int16 r = haydn_x4cmpsel16(a, b, t, f);
  sink_v4 = r;
  return r;
}

// Two independent epochs, reverse consumption order (G-PRED-SSA exit slice).
// IR must keep two distinct cmplt values and two mux uses — not ambient SFR.
// IR-LABEL: @two_epoch_reverse
// IR: call i32 @llvm.haydn.x2cmplt32
// IR: call i32 @llvm.haydn.x2cmplt32
// IR: call <2 x i32> @llvm.haydn.x2mux32
// IR: call <2 x i32> @llvm.haydn.x2mux32
haydn_x2int32 two_epoch_reverse(haydn_x2int32 a1, haydn_x2int32 b1,
                                haydn_x2int32 a2, haydn_x2int32 b2,
                                haydn_x2int32 t1, haydn_x2int32 f1,
                                haydn_x2int32 t2, haydn_x2int32 f2) {
  haydn_pred2_t p1 = haydn_x2cmplt32(a1, b1);
  haydn_pred2_t p2 = haydn_x2cmplt32(a2, b2);
  // Consume p2 first, then p1 (reverse of production).
  haydn_x2int32 r2 = haydn_x2mux32(p2, t2, f2);
  haydn_x2int32 r1 = haydn_x2mux32(p1, t1, f1);
  sink_v2 = r1;
  sink_v2 = r2;
  return haydn_x2add32s(r1, r2);
}
