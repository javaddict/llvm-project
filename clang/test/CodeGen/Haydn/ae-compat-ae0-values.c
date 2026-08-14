// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR0
//
// REQUIRES: haydn-registered-target
//
// Public AE value/arity contracts (independent of object/E96):
//   AE_CVTQ56A32S     — sign-extend <<16 (not constant zero)
//   AE_TRUNCA32X2F64S — satsr64 + unsigned pack; no FIR 32→16 remap
//   AE_CVT16X4 1-arg  — two-operand x4sat32t16 (pad zero)
//   AE_SLAA64S        — ordinary 1<<1 → 2; unsigned sat path
//   AE_SA64NEG_FP     — store-finish dir ImmArg 1 (not POS dir0 alias)
//
// Default fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_TRUNCA32X2F64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVTQ56A32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVT16X4 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_TRUNCA32X2F64S, "ae0.trunca32x2f64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVTQ56A32S, "ae0.cvtq56a32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SA64NEG_FP, "ae0.sa64neg_fp") == 0, "");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64NEG_FP == 1, "SA64NEG dir1");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64POS_FP == 0, "SA64POS dir0");
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 8, "AE-P0 oracle inventory floor");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Host oracle: (int32_t)1 << 16 = 65536. Constant-zero body fails this pin.
// IR-LABEL: @cvtq56_pos
// IR: ret i64 65536
// IR0-LABEL: @cvtq56_pos
ae_int64 cvtq56_pos(void) {
  return AE_CVTQ56A32S(1);
}

// Host oracle: (int32_t)-1 << 16 = 0xFFFFFFFFFFFF0000.
// IR-LABEL: @cvtq56_neg
// IR: ret i64 -65536
// IR0-LABEL: @cvtq56_neg
ae_int64 cvtq56_neg(void) {
  return AE_CVTQ56A32S(-1);
}

// Host oracle: ordinary sat left 1<<1 = 2.
// IR-LABEL: @slaa64s_one_shl1
// IR: ret i64 2
// IR0-LABEL: @slaa64s_one_shl1
ae_int64 slaa64s_one_shl1(void) {
  return AE_SLAA64S((ae_int64)1, 1);
}

// Host oracle: 0x4000... << 1 saturates to INT64_MAX.
// IR-LABEL: @slaa64s_sat_max
// IR: ret i64 9223372036854775807
// IR0-LABEL: @slaa64s_sat_max
ae_int64 slaa64s_sat_max(void) {
  return AE_SLAA64S((ae_int64)0x4000000000000000LL, 1);
}

// Host oracle: negative non-overflow (-3)<<1 = -6 via unsigned path.
// IR-LABEL: @slaa64s_neg_shl1
// IR: ret i64 -6
// IR0-LABEL: @slaa64s_neg_shl1
ae_int64 slaa64s_neg_shl1(void) {
  return AE_SLAA64S((ae_int64)-3, 1);
}

// Host oracle: 0xC000... << 1 saturates to INT64_MIN (not positive minv bug).
// IR-LABEL: @slaa64s_sat_min
// IR: ret i64 -9223372036854775808
// IR0-LABEL: @slaa64s_sat_min
ae_int64 slaa64s_sat_min(void) {
  return AE_SLAA64S((ae_int64)0xC000000000000000LL, 1);
}

// TRUNCA: shift honored as given (32 not remapped to 16).
// satsr64(0x0000000100000000LL, 32) → 1; pack lo=1, hi=0.
// IR-LABEL: @trunca_shift32_no_fir_remap
// IR: ret <2 x i32> <i32 1, i32 0>
// IR0-LABEL: @trunca_shift32_no_fir_remap
ae_int32x2 trunca_shift32_no_fir_remap(void) {
  return AE_TRUNCA32X2F64S((ae_int64)0x0000000100000000LL, (ae_int64)0, 32);
}

// TRUNCA negative high lane: unsigned pack must preserve hi bits.
// Host oracle: lo=5, hi=-1 → <i32 5, i32 -1>.
// IR-LABEL: @trunca_neg_hi_pack
// IR: ret <2 x i32> <i32 5, i32 -1>
// IR0-LABEL: @trunca_neg_hi_pack
ae_int32x2 trunca_neg_hi_pack(void) {
  return AE_TRUNCA32X2F64S((ae_int64)5, (ae_int64)-1, 0);
}

// One-arg CVT16X4 must compile (arity-2 builtin with zero pad).
// IR-LABEL: @cvt16x4_1arg_compiles
// IR: call {{.*}}@llvm.haydn.x4sat32t16
// IR0-LABEL: @cvt16x4_1arg_compiles
// IR0: call {{.*}}@llvm.haydn.x4sat32t16
ae_int16x4 cvt16x4_1arg_compiles(ae_int32x2 a) {
  return AE_CVT16X4(a);
}

// Two-arg CVT16X4 still routes x4sat32t16.
// IR-LABEL: @cvt16x4_2arg
// IR: call {{.*}}@llvm.haydn.x4sat32t16
// IR0-LABEL: @cvt16x4_2arg
ae_int16x4 cvt16x4_2arg(ae_int32x2 a, ae_int32x2 b) {
  return AE_CVT16X4(a, b);
}

// SA64NEG_FP owns dir ImmArg 1 (must not silent-alias POS dir0).
// O2 inlines through wbarwua(ar, ptr, dir); O0 keeps haydn_ae_sa64pos(..., dir).
// IR-LABEL: @sa64neg_dir1
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 1)
// IR0-LABEL: @sa64neg_dir1
// IR0: call void @haydn_ae_sa64pos({{.*}}i32 noundef 1)
void sa64neg_dir1(ae_valign align, void *ptr) {
  AE_SA64NEG_FP(align, ptr);
}

// SA64POS_FP contrast: dir ImmArg 0.
// IR-LABEL: @sa64pos_dir0
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 0)
// IR0-LABEL: @sa64pos_dir0
// IR0: call void @haydn_ae_sa64pos({{.*}}i32 noundef 0)
void sa64pos_dir0(ae_valign align, void *ptr) {
  AE_SA64POS_FP(align, ptr);
}
