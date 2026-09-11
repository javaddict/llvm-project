// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -Werror -Wno-macro-redefined %s
//
// REQUIRES: haydn-registered-target
//
// VEC_CAST law (2026-09-02 O2c wave): an AE_ macro must never C-cast a
// vector operand to a scalar type. The prior bodies
//   AE_SLAI32/SLAA32: ((ae_int32)((ae_int32)(a) << s))
//   AE_SRAI16:        ((ae_int16)((ae_int16)(a) >> s))
//   AE_S16_0_*/AE_S32_L_*: (ae_int16)(src) / (ae_int32)(src)
//   AE_PKSR16:        (ae_int32x2)_pk16lo
// were hard errors on vector operands and would have dropped the non-stored
// lanes. Laws pinned here:
//   * AE_SLAI32/SLAA32 are DUAL-32 lane shifts (corpus vec_scale24x24:119,125
//     Q23<-Q31 left-justify of an ae_f24x2 bag; fir_blms16x16:231 runtime NSA
//     count) — X2SLL32 reg form and X2SRA32 whose golden sign-of-amount law is
//     Xtensa SLA (left when s>=0, ASR when s<0).
//   * AE_SRAI16 is a QUAD-16 ASR (dct_16x16_cffts:160-163) — X4SRA16.
//   * AE_S16_0_* stores lane 0 (raw_lxcorr16x16:295-297 rotates with
//     AE_SEL16_4321 between stores); AE_S32_L_* stores the low 32-bit lane
//     (raw_corr32x32:177 stores the AE_SEL32_LH-rotated lane) — MOVAD32_L.
//   * AE_PKSR16(d, acc, pos) follows the AE_PKSR32 law at 16-bit granularity:
//     d.lane1 = d.lane0 (biquad delay line), d.lane0 = SAT16((acc<<pos +
//     rnd) >> 16) (corpus bqriir16x16_df1:326-333: y0=Bsr stores the NEW
//     output from lane 0; feedback reads lanes {1,0}). Pack via golden
//     X4SAT32T16 with both packed lanes in rsd2: rtd[15:0]=SAT16(rsd2[31:0]),
//     rtd[31:16]=SAT16(rsd2[63:32]).

#include <haydn_dsp.h>

// IR-LABEL: @slai32_dual_lane_shift
// Both lanes shift; never a scalar C cast of the vector.
// IR: call <2 x i32> @llvm.haydn.x2sll32(<2 x i32> {{.*}}, i32 8)
ae_int32x2 slai32_dual_lane_shift(ae_int32x2 v) {
  return AE_SLAI32(v, 8);
}

// IR-LABEL: @slai32_f24_bag_input
// ae_f24x2 bag input flows through __AE_AS_V2 bit-identically (Q23<-Q31).
// IR: call <2 x i32> @llvm.haydn.x2sll32(<2 x i32> {{.*}}, i32 8)
ae_f32x2 slai32_f24_bag_input(ae_f24x2 vx0p) {
  return AE_SLAI32(vx0p, 8);
}

// IR-LABEL: @slaa32_reg_direction
// Runtime amount; X2SRA32 sign-of-amount law (SLA semantics).
// IR: call <2 x i32> @llvm.haydn.x2sra32(<2 x i32> {{.*}}, i32 %{{.*}})
ae_int32x2 slaa32_reg_direction(ae_int32x2 vxw, int s_exp) {
  return AE_SLAA32(vxw, s_exp);
}

// IR-LABEL: @srai16_quad_lane_shift
// IR: call <4 x i16> @llvm.haydn.x4sra16(<4 x i16> {{.*}}, i32 2)
ae_int16x4 srai16_quad_lane_shift(ae_int16x4 vA0s) {
  return AE_SRAI16(vA0s, 2);
}

// IR-LABEL: @s16_0_lane0_store
// Lane 0 of the quad goes to memory; the optimizer sees through the
// __AE_TO_I64 bag round-trip and folds the extract (never a scalar C cast
// of the whole vector).
// IR: extractelement <4 x i16> {{.*}}, i64 0
// IR: store i16 {{.*}}, ptr {{.*}}
ae_int16 s16_0_lane0_store(ae_int16x4 x0, ae_int16 *R) {
  AE_S16_0_IP(x0, R, -2);
  x0 = AE_SEL16_4321(x0, x0);
  AE_S16_0_IP(x0, R, -2);
  return x0[0];
}

// IR-LABEL: @s32_l_low_lane_store
// Low 32-bit lane of the ae_f32x2 pair (MOVAD32_L), scalar advance kept.
// IR: call i32 @llvm.haydn.movad32.low(i64 {{.*}})
void s32_l_low_lane_store(ae_f32x2 vy0f, ae_int32 *py) {
  AE_S32_L_I(vy0f, py, 0);
}

// Scalar src passes through __AE_TO_I64 (sign-extend) then MOVAD32_L; value
// is unchanged — no lane drop, no splat.
// IR-LABEL: @scalar_src_passthrough
// IR: sext i32 {{.*}} to i64
// IR: call i32 @llvm.haydn.movad32.low(i64 {{.*}})
void scalar_src_passthrough(ae_int32 s, ae_int32 *p) {
  AE_S32_L_IP(s, p, 4);
}

// IR-LABEL: @pksr16_delay_line_pack
// Round 64->16 via SRA64R (shift 16-pos) then the X4SAT32T16 pack with the
// old lane 0 preserved into lane 1 (delay line), new value at lane 0.
// IR: call i64 @llvm.haydn.sra64r(i64 {{.*}}, i32 14)
// IR: call <4 x i16> @llvm.haydn.x4sat32t16(<2 x i32> {{.*}}, <2 x i32> {{.*}})
ae_int16x4 pksr16_delay_line_pack(ae_int16x4 Asr, ae_int64 acc0) {
  AE_PKSR16(Asr, acc0, 2);
  return Asr;
}
