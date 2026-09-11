// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only %s
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O0 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -c -o %t.o %s
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
//
// T-DSP3 exactness seats (compile + host IR oracle). Default strict:
//   vec_dot16  — dest-typed AE_MULAF16X4SS host (1190); AE_MULAAAAQ16
//                unconditional Path-A fmulaa16.hs.11.00 (two-lane host 380)
//   firinterp  — statement-form AE_MULFD32X16X2_FIR_HH write-back
//   mtx_mpy    — statement-form AE_MULAAD32X16 write-back (integer, not Q31)
// Empty output fails. X2CMUL public wrappers stay fail-closed.

#include <haydn_dsp.h>

_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
#ifndef AE_MULAAAAQ16
_Static_assert(0, "AE_MULAAAAQ16 must be defined unconditionally");
#endif
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAAAAQ16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULAAAAQ16,
                                "emu.mulaaaaq16") == 0,
               "");
#if defined(AE_FIR_NATIVE_COMPOSITE) || defined(AE_FFT_NATIVE_COMPOSITE)
_Static_assert(0, "do not invent a native FIR/FFT composite ISA");
#endif

// Documented 32-bit sat host of the Wave-8 vector. Do not retarget to 380.
// IR-LABEL: @tdsp3_vec_dot16_host_1190
// IR: ret i32 1190
int32_t tdsp3_vec_dot16_host_1190(void) {
  const int16_t x[8] = {10, -10, 20, -20, 30, 40, 50, 60};
  const int16_t y[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int acc = 0;
  for (int i = 0; i < 8; ++i)
    acc = haydn_satsr64((int64_t)acc + (int64_t)x[i] * (int64_t)y[i], 0);
  return acc;
}

// Documented two-lane integer host of the same Wave-8 vector (lanes 0+1
// per quad). AE_MULAAAAQ16 Path-A body. Do not retarget 1190 onto this.
// IR-LABEL: @tdsp3_two_lane_host_380
// IR: ret i32 380
int32_t tdsp3_two_lane_host_380(void) {
  const int16_t x[8] = {10, -10, 20, -20, 30, 40, 50, 60};
  const int16_t y[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int acc = 0;
  for (int i = 0; i < 8; i += 4)
    acc = haydn_satsr64((int64_t)acc + (int64_t)x[i] * (int64_t)y[i] +
                            (int64_t)x[i + 1] * (int64_t)y[i + 1],
                        0);
  return acc;
}

// Flipped Path-A body: AE_MULAAAAQ16 -> fmulaa16.hs.11.00, not X4MULA16S.
// IR-LABEL: @tdsp3_mulaaaaq16_path_a
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00
// IR-NOT: x4mula16s
// OBJ-LABEL: <tdsp3_mulaaaaq16_path_a>:
// OBJ: {{fmulaa16|FMULAA16}}
ae_int64 tdsp3_mulaaaaq16_path_a(ae_int16x4 a, ae_int16x4 b) {
  ae_int64 acc = AE_ZERO64();
  AE_MULAAAAQ16(acc, a, b);
  return acc;
}

// NatureDSP vec_dot16x16_fast `#ifndef AE_MULAAAAQ16` class now takes Path-A.
// IR-LABEL: @tdsp3_guarded_kernel_mulaaaaq16
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00
// IR-NOT: x4mula16s
// OBJ-LABEL: <tdsp3_guarded_kernel_mulaaaaq16>:
// OBJ: {{fmulaa16|FMULAA16}}
int32_t tdsp3_guarded_kernel_mulaaaaq16(ae_int16x4 x, ae_int16x4 y) {
#ifndef AE_MULAAAAQ16
  ae_f32x2 vaf = AE_MOVI(0);
  ae_f32x2 vbf = AE_MOVI(0);
  AE_MULAF16X4SS(vaf, vbf, x, y);
  ae_int32x2 vai = (ae_int32x2)vaf;
  ae_int32x2 vbi = (ae_int32x2)vbf;
  vai = AE_ADD32S(vai, vbi);
  vbi = AE_SEL32_LH(vai, vai);
  vai = AE_ADD32S(vai, vbi);
  return AE_MOVAD32_H(vai);
#else
  ae_int64 acc = AE_ZERO64();
  AE_MULAAAAQ16(acc, x, y);
  ae_int32x2 t = AE_TRUNCA32X2F64S(acc, acc, 33);
  return AE_MOVAD32_L(t);
#endif
}

// vec_dot16x16_fast documented path: dest-typed ae_f32x2 accs through
// X4MULA16S then ADD32S / SEL32_LH / MOVAD32_H. Target MAC does not fold.
// Writeback is a union bitcast, not a C i64-><2 x i32> splat.
// IR-LABEL: @tdsp3_vec_dot16_ones
// IR: call { i64, i64 } @llvm.haydn.x4mula16s
// IR: bitcast i64 {{.*}} to <2 x i32>
// IR: bitcast i64 {{.*}} to <2 x i32>
// IR: call <2 x i32> @llvm.haydn.x2add32s
// IR: call i32 @llvm.haydn.movad32.high
// OBJ-LABEL: <tdsp3_vec_dot16_ones>:
// OBJ: {{x4mula16s|mula}}
int32_t tdsp3_vec_dot16_ones(void) {
  ae_f32x2 vaf = AE_MOVI(0);
  ae_f32x2 vbf = AE_MOVI(0);
  ae_int16x4 x = AE_MOVDA16(1);
  ae_int16x4 y = AE_MOVDA16(1);
  AE_MULAF16X4SS(vaf, vbf, x, y);
  ae_int32x2 vai = (ae_int32x2)vaf;
  ae_int32x2 vbi = (ae_int32x2)vbf;
  vai = AE_ADD32S(vai, vbi);
  vbi = AE_SEL32_LH(vai, vai);
  vai = AE_ADD32S(vai, vbi);
  return AE_MOVAD32_H(vai);
}

// Dest-typed writeback is a union bitcast, not a C splat.
// i64 0x0000000200000001 -> <2 x i32> {1, 2} = 8589934593.
// A splat of the low 32 bits would be {1, 1} = 4294967297.
// IR-LABEL: @tdsp3_dest_typed_assign_bits
// IR: ret i64 8589934593
// OBJ-LABEL: <tdsp3_dest_typed_assign_bits>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int64_t tdsp3_dest_typed_assign_bits(void) {
  ae_f32x2 v;
  __AE_ASSIGN_BITS(v, 0x0000000200000001LL);
  return __AE_TO_I64(v);
}

// firinterp dual-acc FIR init writes both q0 and q1 (not pointer-only).
// IR-LABEL: @tdsp3_firinterp_mulfd_writes
// IR: call i64 @llvm.haydn.ff2mula32rs.hh
// OBJ-LABEL: <tdsp3_firinterp_mulfd_writes>:
// OBJ: {{ff2mula32rs|fmul|mula}}
void tdsp3_firinterp_mulfd_writes(ae_int64 *q0, ae_int64 *q1, ae_int32x2 d0,
                                  ae_int32x2 d1, ae_int16x4 c) {
  ae_int64 a = 0, b = 0;
  AE_MULFD32X16X2_FIR_HH(a, b, d0, d1, c);
  *q0 = a;
  *q1 = b;
}

// mtx_mpy statement form writes acc via integer mula64 (not Q31 F2MULAA).
// H3_L2 widens lanes 3/2; H1_L0 widens lanes 1/0.
// IR-LABEL: @tdsp3_mtx_mpy_mulaad_h3l2
// IR: call i64 @llvm.haydn.mula64.ss.hh
// IR: call i64 @llvm.haydn.mula64.ss.ll
// OBJ-LABEL: <tdsp3_mtx_mpy_mulaad_h3l2>:
// OBJ: {{mula64|mul}}
ae_int64 tdsp3_mtx_mpy_mulaad_h3l2(void) {
  ae_int64 c = AE_ZERO64();
  ae_int32x2 x = AE_MOVDA32X2(2, 3);
  ae_int16x4 y = AE_MOVDA16(4, 3, 2, 1);
  AE_MULAAD32X16_H3_L2(c, x, y);
  return c;
}

// IR-LABEL: @tdsp3_mtx_mpy_mulaad_h1l0
// IR: call i64 @llvm.haydn.mula64.ss.hh
// IR: call i64 @llvm.haydn.mula64.ss.ll
// OBJ-LABEL: <tdsp3_mtx_mpy_mulaad_h1l0>:
// OBJ: {{mula64|mul}}
ae_int64 tdsp3_mtx_mpy_mulaad_h1l0(void) {
  ae_int64 c = AE_ZERO64();
  ae_int32x2 x = AE_MOVDA32X2(2, 3);
  ae_int16x4 y = AE_MOVDA16(4, 3, 2, 1);
  AE_MULAAD32X16_H1_L0(c, x, y);
  return c;
}
