// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O2 \
// RUN:   -mllvm -enable-post-misched=false -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target
//
// C1.2 / G-PRED-SSA exit gate (AE_* wrappers + ambient quarantine):
//   AE_LT32 / AE_SLT16X4 → pure i32 cmplt (no vector→xtbool cast).
//   AE_EQ32 / AE_LE32 → ambient compare + movesfr2gpr capture.
//   AE_MOVT*/AE_MOVF* → x2/x4mux with explicit cond (cond not dropped).
//   Ambient two-epoch reverse-order keeps two ordered x2movt32 (no pure CSE).
//   x2movt32 declare is IntrHasSideEffects (not memory(none)/speculatable).

#include <haydn_dsp.h>

volatile ae_int32x2 sink_v2;
volatile ae_int16x4 sink_v4;
volatile xtbool2 sink_b2;
volatile xtbool4 sink_b4;
volatile ae_int64 sink_i64;

// IR-LABEL: @test_ae_lt32
// IR: call i32 @llvm.haydn.x2cmplt32
// IR-NOT: call {{.*}} @llvm.haydn.x2slt32
xtbool2 test_ae_lt32(ae_int32x2 a, ae_int32x2 b) {
  xtbool2 p = AE_LT32(a, b);
  sink_b2 = p;
  return p;
}

// IR-LABEL: @test_ae_slt16x4
// IR: call i32 @llvm.haydn.x4cmplt16
// IR-NOT: call {{.*}} @llvm.haydn.x4slt16
xtbool4 test_ae_slt16x4(ae_int16x4 a, ae_int16x4 b) {
  xtbool4 p = AE_SLT16X4(a, b);
  sink_b4 = p;
  return p;
}

// IR-LABEL: @test_ae_eq32
// IR: call <2 x i32> @llvm.haydn.x2seq32
// IR: call i32 @llvm.haydn.movesfr2gpr
xtbool2 test_ae_eq32(ae_int32x2 a, ae_int32x2 b) {
  xtbool2 p = AE_EQ32(a, b);
  sink_b2 = p;
  return p;
}

// IR-LABEL: @test_ae_le32
// IR: call <2 x i32> @llvm.haydn.x2sle32
// IR: call i32 @llvm.haydn.movesfr2gpr
xtbool2 test_ae_le32(ae_int32x2 a, ae_int32x2 b) {
  xtbool2 p = AE_LE32(a, b);
  sink_b2 = p;
  return p;
}

// IR-LABEL: @test_ae_movt32x2
// IR: call <2 x i32> @llvm.haydn.x2mux32
// IR-NOT: call {{.*}} @llvm.haydn.x2movt32
ae_int32x2 test_ae_movt32x2(ae_int32x2 dst, ae_int32x2 src, xtbool2 cond) {
  AE_MOVT32X2(dst, src, cond);
  sink_v2 = dst;
  return dst;
}

// IR-LABEL: @test_ae_movf32x2
// IR: call <2 x i32> @llvm.haydn.x2mux32
// IR-NOT: call {{.*}} @llvm.haydn.x2movf32
ae_int32x2 test_ae_movf32x2(ae_int32x2 dst, ae_int32x2 src, xtbool2 cond) {
  AE_MOVF32X2(dst, src, cond);
  sink_v2 = dst;
  return dst;
}

// IR-LABEL: @test_ae_movt16x4
// IR: call <4 x i16> @llvm.haydn.x4mux16
ae_int16x4 test_ae_movt16x4(ae_int16x4 dst, ae_int16x4 src, xtbool4 cond) {
  AE_MOVT16X4(dst, src, cond);
  sink_v4 = dst;
  return dst;
}

// IR-LABEL: @test_ae_movf16x4
// IR: call <4 x i16> @llvm.haydn.x4mux16
ae_int16x4 test_ae_movf16x4(ae_int16x4 dst, ae_int16x4 src, xtbool4 cond) {
  AE_MOVF16X4(dst, src, cond);
  sink_v4 = dst;
  return dst;
}

// 2-arg AE_MOVT64 stays ambient movt64 (no cond).
// IR-LABEL: @test_ae_movt64_2arg
// IR: call i64 @llvm.haydn.movt64
ae_int64 test_ae_movt64_2arg(ae_int64 dst, ae_int64 src) {
  AE_MOVT64(dst, src);
  sink_i64 = dst;
  return dst;
}

// Ambient two-epoch reverse-order: two slt + two movt must remain distinct
// ordered side-effecting calls (cannot CSE pure-movt at O0 or O2).
// IR-LABEL: @ambient_two_epoch_reverse
// IR: call <2 x i32> @llvm.haydn.x2slt32
// IR: call <2 x i32> @llvm.haydn.x2movt32
// IR: call <2 x i32> @llvm.haydn.x2slt32
// IR: call <2 x i32> @llvm.haydn.x2movt32
// x2movt32 must share side-effect attrs with x2slt32 (not pure #memory(none)).
// IR: declare {{.*}} @llvm.haydn.x2slt32{{.*}} #[[SFRATTR:[0-9]+]]
// IR: declare {{.*}} @llvm.haydn.x2movt32{{.*}} #[[SFRATTR]]
// IR: attributes #[[SFRATTR]] = { {{.*}}willreturn{{.*}} }
// IR-NOT: attributes #[[SFRATTR]] = { {{.*}}speculatable{{.*}} }
// IR-NOT: attributes #[[SFRATTR]] = { {{.*}}memory(none){{.*}} }
ae_int32x2 ambient_two_epoch_reverse(ae_int32x2 a1, ae_int32x2 b1,
                                     ae_int32x2 a2, ae_int32x2 b2,
                                     ae_int32x2 t1, ae_int32x2 f1,
                                     ae_int32x2 t2, ae_int32x2 f2) {
  // Epoch 1: set SFR from (a1 < b1), cmov into r1.
  (void)haydn_x2slt32(a1, b1);
  ae_int32x2 r1 = (ae_int32x2)haydn_x2movt32(f1, t1);
  // Epoch 2: overwrite SFR with (a2 < b2), cmov into r2.
  (void)haydn_x2slt32(a2, b2);
  ae_int32x2 r2 = (ae_int32x2)haydn_x2movt32(f2, t2);
  sink_v2 = r1;
  sink_v2 = r2;
  return (ae_int32x2)haydn_x2add32s(r1, r2);
}
