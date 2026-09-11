// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding -Werror -Wno-macro-redefined %s
//
// REQUIRES: haydn-registered-target
//
// TYPE-coercion law (2026-09-02 O2c-2): HiFi has one 64-bit AE type;
// Haydn maps it to GNU vector (ae_int32x2), scalar bag (ae_f24x2 =
// long long), and scalar (ae_int32). C forbids crossing them
// implicitly. Named macros must use __AE_ASSIGN_BITS / __AE_TO_I64 /
// splat, never a C-cast of vector↔bag↔scalar.

#include <haydn_dsp.h>

// IR-LABEL: @l32x2f24_ip_bag_dst
// Two u32 words into the ae_f24x2 bag via ASSIGN_BITS — never
// `bag = *(ae_int32x2 *)`.
// IR: load i32
// IR: load i32
// IR: store i64
void l32x2f24_ip_bag_dst(ae_f24x2 *dst, const ae_f24x2 *p) {
  AE_L32X2F24_IP(*dst, p, 8);
}

// IR-LABEL: @l16_ip_quad_dst_not_broadcast
// Scalar 16-bit load into ae_int16x4 is ASSIGN_BITS (zero-extend), not
// a 4-lane splat. Kernels that then SEL16 the quad (vec_cplx2real) must
// not see a silent broadcast.
// IR: load i16
// IR: insertelement <4 x i16>
// IR-NOT: shufflevector <4 x i16> {{.*}} zeroinitializer
ae_int16x4 l16_ip_quad_dst_not_broadcast(const ae_int16 *p) {
  ae_int16x4 v;
  AE_L16_IP(v, p, 2);
  return v;
}

// IR-LABEL: @movi_broadcast_both_lanes
// Folded splat is both lanes; never a scalar→vector C-cast.
// IR: ret <2 x i32> splat (i32 1)
ae_int32x2 movi_broadcast_both_lanes(void) {
  return AE_MOVI(1);
}

// IR-LABEL: @mulafp_vector_acc
// acc is ae_f32x2; wrap __AE_TO_I64(acc) into ff2mula32rs_ll and
// ASSIGN_BITS the i64 result back. Never pass the vector raw.
// IR: call i64 @llvm.haydn.ff2mula32rs
void mulafp_vector_acc(ae_f32x2 *acc, ae_int32x2 a, ae_int32x2 b) {
  AE_MULAFP32X2RAS(*acc, a, b);
}

// IR-LABEL: @movint32x2_from_f24_bag
// IR: bitcast i64 {{.*}} to <2 x i32>
ae_int32x2 movint32x2_from_f24_bag(ae_f24x2 a) {
  return AE_MOVINT32X2_FROMF24X2(a);
}

// IR-LABEL: @s32x2f24_ip_bag_src
// Store twin: __AE_TO_I64(src) written as the 8-byte bag (optimizer
// sees through the v2 bitcast).
// IR: store i64 %src
void s32x2f24_ip_bag_src(ae_f24x2 src, ae_f24x2 *p) {
  AE_S32X2F24_IP(src, p, 8);
}

// IR-LABEL: @l32f24_ip_bag_dst
// Scalar-24 load into ae_f24x2 must not go through late AE_L32_IP
// (`dst = AE_MOVDA32(w)` is vector-into-bag). Broadcast bits via
// ASSIGN_MOVDA32.
// IR: load i32
// IR: store i64
void l32f24_ip_bag_dst(ae_f24x2 *dst, const ae_f24 *p) {
  AE_L32F24_IP(*dst, p, 4);
}

// IR-LABEL: @la32x2f24_ip_bag_dst
// la64_step returns ae_int32x2; dest is the f24 bag. ASSIGN_BITS, never
// `(ae_f24x2)vector`.
void la32x2f24_ip_bag_dst(ae_f24x2 *dst, ae_valign *al, ae_f24x2 *p) {
  AE_LA32X2F24_IP(*dst, *al, p);
}

// IR-LABEL: @mulafp24_vector_acc
// Same dest-typed writeback as MULAFP32; acc is ae_f32x2.
// IR: call i64 @llvm.haydn.ff2mula32rs
void mulafp24_vector_acc(ae_f32x2 *acc, ae_int32x2 a, ae_int32x2 b) {
  AE_MULAFP24X2RA(*acc, a, b);
}

// IR-LABEL: @l16_xc_quad_dst_not_broadcast
// Circular scalar-16 into ae_int16x4 is ASSIGN_BITS (zero-extend), not
// a 4-lane splat. Same law as AE_L16_IP.
// IR: load i16
// IR: insertelement <4 x i16>
// IR-NOT: shufflevector <4 x i16> {{.*}} zeroinitializer
ae_int16x4 l16_xc_quad_dst_not_broadcast(ae_int16 *p) {
  ae_int16x4 v;
  AE_L16_XC(v, p, 2);
  return v;
}

// IR-LABEL: @l32_xc_broadcast
// Scalar-32 circular load into ae_int32x2 is MOVDA32 (cxfir HH+LL).
// Optimizer folds ASSIGN_MOVDA32 to zext+shl+or+bitcast.
// IR: load i32
// IR: shl {{.*}} 32
// IR: bitcast i64 {{.*}} to <2 x i32>
ae_int32x2 l32_xc_broadcast(ae_int32 *p) {
  ae_int32x2 v;
  AE_L32_XC(v, p, 4);
  return v;
}

// IR-LABEL: @l32_xp_broadcast
// Same MOVDA32 law as late AE_L32_IP. Dest is ae_int32x2.
// IR: load i32
// IR: shl {{.*}} 32
// IR: bitcast i64 {{.*}} to <2 x i32>
ae_int32x2 l32_xp_broadcast(const ae_int32 *p) {
  ae_int32x2 v;
  AE_L32_XP(v, p, 4);
  return v;
}

// IR-LABEL: @s16x2m_i_store_lo
// Store the low 32-bit lane; never `*(ae_int32*) = vector`.
// IR: store i32
void s16x2m_i_store_lo(ae_int32x2 src, ae_int32 *p) {
  AE_S16X2M_I(src, p, 0);
}

// IR-LABEL: @lt32_vs_scalar_zero
// Scalar 0 becomes a dual-32 compare operand via __AE_AS_V2 (0,0).
// IR: call {{.*}} @llvm.haydn.x2cmplt32
xtbool2 lt32_vs_scalar_zero(ae_int32x2 x) {
  return AE_LT32(x, 0);
}

// IR-LABEL: @movint16x4_fromf16_broadcast
// Scalar f16 → quad is AE_MOVDA16, never a C-cast splat of different size.
// IR: ret <4 x i16> splat (i16 7)
ae_int16x4 movint16x4_fromf16_broadcast(void) {
  return AE_MOVINT16X4_FROMF16((ae_int16)7);
}

// IR-LABEL: @nsaz32_l_accepts_vector_bag
// NatureDSP AE_NSAZ32_L(ae_int32x2) — wrap through __AE_TO_I64, never
// pass the vector to int64_t haydn_nsaz32_l.
// IR: call i32 @llvm.haydn.nsaz32.l(i64
int nsaz32_l_accepts_vector_bag(ae_int32x2 vxw) {
  return AE_NSAZ32_L(vxw);
}

// IR-LABEL: @l32_i_2arg_assigns_to_bag
// 2-arg AE_L32_I is a returning MOVDA32 (both lanes), assigned to
// ae_int32x2. Never `ae_int32` scalar into the vector bag.
// IR: load i32
// IR: shl {{.*}} 32
ae_int32x2 l32_i_2arg_assigns_to_bag(const ae_int32 *p) {
  ae_int32x2 x;
  x = AE_L32_I(p, 0);
  return x;
}

// IR-LABEL: @l16_i_2arg_assigns_to_quad
// 2-arg AE_L16_I zero-extends into ae_int16x4 (same law as AE_L16_IP).
// IR: load i16
// IR: insertelement <4 x i16>
// IR-NOT: shufflevector <4 x i16> {{.*}} zeroinitializer
ae_int16x4 l16_i_2arg_assigns_to_quad(const ae_int16 *p) {
  ae_int16x4 v;
  v = AE_L16_I(p, 0);
  return v;
}

// IR-LABEL: @scalar_zero_init_i32x2
// NatureDSP `ae_int32x2 zt=0` — ext_vector accepts scalar 0 (splat).
// IR: ret <2 x i32> zeroinitializer
ae_int32x2 scalar_zero_init_i32x2(void) {
  ae_int32x2 zt = 0;
  return zt;
}

// IR-LABEL: @scalar_zero_init_i16x4
// IR: ret <4 x i16> zeroinitializer
ae_int16x4 scalar_zero_init_i16x4(void) {
  ae_int16x4 sum = 0;
  return sum;
}

// IR-LABEL: @eq16_returns_xtbool4
// NatureDSP `xtbool4 b = AE_EQ16(x, y)` — SFR capture, never assign
// haydn_x4int16 into xtbool4.
// IR: call i32 @llvm.haydn.movesfr2gpr
xtbool4 eq16_returns_xtbool4(ae_int16x4 a, ae_int16x4 b) {
  return AE_EQ16(a, b);
}

// IR-LABEL: @movba4_is_xtbool4
// NatureDSP `xtbool4 bmask = AE_MOVBA4(8)` — 4-bit pred, not a v4 bag.
// IR: ret i32 8
xtbool4 movba4_is_xtbool4(void) {
  return AE_MOVBA4(8);
}

// IR-LABEL: @mulfp32x16x2ras_h_2arg
// HiFi MULFP is 2-arg returning. Wrapper is GNU-vector typed; zero acc
// via AE_ZERO32(), a via V2, b via V4. Literal 0 / i64 bags are
// incompatible (or would splat).
// IR: call i64 @llvm.haydn.mulfp32x16x2ras.high(i64 0, i64 {{.*}}, i64 {{.*}})
// IR-NOT: shufflevector <4 x i16> {{.*}} zeroinitializer
ae_f32x2 mulfp32x16x2ras_h_2arg(ae_f32x2 a, ae_f16x4 b) {
  return AE_MULFP32X16X2RAS_H(a, b);
}

// IR-LABEL: @mulfp32x16x2ras_l_2arg
// IR: call i64 @llvm.haydn.mulfp32x16x2ras.low(i64 0, i64 {{.*}}, i64 {{.*}})
ae_f32x2 mulfp32x16x2ras_l_2arg(ae_f32x2 a, ae_f16x4 b) {
  return AE_MULFP32X16X2RAS_L(a, b);
}

// IR-LABEL: @mulc32x16_h_uses_native_x2cmul32x16
// Exact wrap: haydn_x2cmul32x16_h via __AE_TO_I64 / __AE_I2V. Never
// X2CMUL32 (2-dest 32x32) and never a C-cast splat of the i64 dest.
// IR: call i64 @llvm.haydn.x2cmul32x16.h
// IR-NOT: call {{.*}}@llvm.haydn.x2cmul32{{[^x]}}
ae_int32x2 mulc32x16_h_uses_native_x2cmul32x16(ae_int32x2 a, ae_int16x4 b) {
  return AE_MULC32X16_H(a, b);
}

// IR-LABEL: @mulc32x16_l_uses_native_x2cmul32x16
// IR: call i64 @llvm.haydn.x2cmul32x16.l
// IR-NOT: call {{.*}}@llvm.haydn.x2cmul32{{[^x]}}
ae_int32x2 mulc32x16_l_uses_native_x2cmul32x16(ae_int32x2 a, ae_int16x4 b) {
  return AE_MULC32X16_L(a, b);
}

// IR-LABEL: @movda16x2_returns_i16x4
// Two 16-bit immediates pack into ae_int16x4 (lo, hi, 0, 0), not ae_int32x2.
// 0x5678=22136, 0x1234=4660.
// IR: ret <4 x i16> <i16 22136, i16 4660, i16 0, i16 0>
ae_int16x4 movda16x2_returns_i16x4(void) {
  return AE_MOVDA16X2(0x1234, 0x5678);
}

// IR-LABEL: @mulafp32x2rs_vector_acc
// acc is ae_f32x2; wrap __AE_TO_I64(acc) into ff2mula32rs_ll.
// IR: call i64 @llvm.haydn.ff2mula32rs
void mulafp32x2rs_vector_acc(ae_f32x2 *acc, ae_int32x2 a, ae_int32x2 b) {
  AE_MULAFP32X2RS(*acc, a, b);
}

// IR-LABEL: @mulaf16ss_11_vector_acc
// IR: call i64 @llvm.haydn.fmul16.hs11
void mulaf16ss_11_vector_acc(ae_f32x2 *acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULAF16SS_11(*acc, a, b);
}

// IR-LABEL: @mulsf16ss_20_vector_acc
// haydn_fmul16_hs20 is the commutative hs02 alias (operands swapped).
// IR: call i64 @llvm.haydn.fmul16.hs02
void mulsf16ss_20_vector_acc(ae_f32x2 *acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULSF16SS_20(*acc, a, b);
}

// IR-LABEL: @mulafc16ras_vector_acc
// Never C-cast vector acc to ae_int64; wrap __AE_TO_I64.
// IR: call <4 x i16> @llvm.haydn.x4fcmula16rs
void mulafc16ras_vector_acc(ae_int16x4 *acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULAFC16RAS(*acc, a, b);
}

// IR-LABEL: @mulaar16p16x4s_vector_acc
// Quad-16 acc: __AE_TO_I64 in, ASSIGN_BITS out. Never (int64_t)(acc).
// IR: call {{.*}} @llvm.haydn.x4mula16s
void mulaar16p16x4s_vector_acc(ae_int16x4 *acc, ae_int16x4 a, ae_int16x4 b) {
  AE_MULAAR16P16X4S_vector(*acc, a, b);
}

// IR-LABEL: @sel32_lh_scalar_zero
// Scalar 0 becomes a dual-32 select operand via __AE_AS_V2.
// IR: call <2 x i32> @llvm.haydn.x2sel32.lh
ae_int32x2 sel32_lh_scalar_zero(ae_int32x2 a) {
  return AE_SEL32_LH(a, 0);
}

// IR-LABEL: @sext32x2d16_32_from_i16x4
// Identity bag reinterpret: (ae_int32x2)__AE_AS_V2(a), not a C splat.
// IR: bitcast <4 x i16> {{.*}} to <2 x i32>
ae_int32x2 sext32x2d16_32_from_i16x4(ae_int16x4 a) {
  return AE_SEXT32X2D16_32(a);
}

// IR-LABEL: @sext32x2d16_10_from_i16x4
// IR: bitcast <4 x i16> {{.*}} to <2 x i32>
ae_int32x2 sext32x2d16_10_from_i16x4(ae_int16x4 a) {
  return AE_SEXT32X2D16_10(a);
}
