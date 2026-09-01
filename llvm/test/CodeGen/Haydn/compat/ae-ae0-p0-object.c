// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -mllvm -stop-after=instruction-select -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=ISEL
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -mllvm -stop-after=haydn-verify-bundles -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=MIR
//
// AE-P0 public surface: value + committed-member oracles (empty output fails).
//   CVTQ56A32S / SLAA64S / TRUNCA32X2F64S — host value oracles
//   CVT16X4 1-arg — non-empty X4SAT32T16 selected member
//   SA64NEG vs SA64POS — dir ImmArg 1 vs 0 survives IR + ISEL; golden e0 is 2-op
// Default fail-closed (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_CVTQ56A32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_TRUNCA32X2F64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_CVT16X4 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64POS_FP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64NEG_FP == 1, "SA64NEG dir1");
_Static_assert(HAYDN_AE_DIRIMM_AE_SA64POS_FP == 0, "SA64POS dir0");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_SA64NEG_FP == 1, "store-finish");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_SA64POS_FP == 1, "store-finish");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVTQ56A32S, "ae0.cvtq56a32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_TRUNCA32X2F64S, "ae0.trunca32x2f64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA64S, "ae0.slaa64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SA64NEG_FP, "ae0.sa64neg_fp") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SA64POS_FP, "ae0.sa64pos_fp") == 0, "");
// AE-P0 + full EXACT tier + softsat residual public oracle inventory floor.
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 671, "full public AE oracle inventory floor");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Scalar TRUNCA32F64S: SAT32(acc >> shift), no invented extra headroom.
// IR-LABEL: @ae0_trunca32f64s
// IR: ret i32 5
// MIR-LABEL: name: ae0_trunca32f64s
// MIR: {{ADDI32|LOADI32}}
ae_int32 ae0_trunca32f64s(void) {
  return AE_TRUNCA32F64S((ae_int64)5, 0);
}

// Host oracle: (int32_t)1 << 16 = 65536.
// IR-LABEL: @ae0_cvtq56_pos
// IR: ret i64 65536
// MIR-LABEL: name: ae0_cvtq56_pos
// MIR: {{ADDI32|LOADI32}}
ae_int64 ae0_cvtq56_pos(void) {
  return AE_CVTQ56A32S(1);
}

// Host oracle: (int32_t)-1 << 16 = -65536.
// IR-LABEL: @ae0_cvtq56_neg
// IR: ret i64 -65536
// MIR-LABEL: name: ae0_cvtq56_neg
// MIR: {{ADDI32|LOADI32}}
ae_int64 ae0_cvtq56_neg(void) {
  return AE_CVTQ56A32S(-1);
}

// Host oracle: ordinary sat left 1<<1 = 2.
// IR-LABEL: @ae0_slaa64s_one
// IR: ret i64 2
// MIR-LABEL: name: ae0_slaa64s_one
// MIR: {{ADDI32|LOADI32}}
ae_int64 ae0_slaa64s_one(void) {
  return AE_SLAA64S((ae_int64)1, 1);
}

// Host oracle: 0x4000... << 1 saturates to INT64_MAX.
// IR-LABEL: @ae0_slaa64s_sat_max
// IR: ret i64 9223372036854775807
// MIR-LABEL: name: ae0_slaa64s_sat_max
// MIR: {{ADDI32|LOADI32}}
ae_int64 ae0_slaa64s_sat_max(void) {
  return AE_SLAA64S((ae_int64)0x4000000000000000LL, 1);
}

// TRUNCA negative high lane: unsigned pack preserves hi bits → 5, -1.
// IR-LABEL: @ae0_trunca_pack
// IR: ret <2 x i32> <i32 5, i32 -1>
// MIR-LABEL: name: ae0_trunca_pack
// MIR: {{ADDI32|LOADI32}}
ae_int32x2 ae0_trunca_pack(void) {
  return AE_TRUNCA32X2F64S((ae_int64)5, (ae_int64)-1, 0);
}

// One-arg CVT16X4 → two-operand x4sat32t16 (zero pad); non-empty member.
// IR-LABEL: @ae0_cvt16x4_1arg
// IR: call {{.*}}@llvm.haydn.x4sat32t16
// MIR-LABEL: name: ae0_cvt16x4_1arg
// MIR: X4SAT32T16
ae_int16x4 ae0_cvt16x4_1arg(ae_int32x2 a) {
  return AE_CVT16X4(a);
}

// SA64NEG_FP owns dir ImmArg 1 (must not silent-alias POS dir0).
// Direction is an ImmArg on the intrinsic (IR); CB-151 folds it at ISel —
// the logical/member wire shape is 2-op (ar_sel, dest2); dir is not a
// Format E field. Distinct ar_sel still distinguishes the streams.
// IR-LABEL: @ae0_sa64neg_dir1
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 1)
// ISEL-LABEL: name: ae0_sa64neg_dir1
// ISEL-DAG: WBARWUA 0,
// ISEL-DAG: WBARWUA 1,
// MIR-LABEL: name: ae0_sa64neg_dir1
// MIR: WBARWUA{{.*}}_AR
// MIR: WBARWUA{{.*}}_AR
void ae0_sa64neg_dir1(ae_valign align, void *ptr) {
  AE_SA64NEG_FP(align, ptr);
}

// SA64POS_FP contrast: dir ImmArg 0.
// IR-LABEL: @ae0_sa64pos_dir0
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 0)
// ISEL-LABEL: name: ae0_sa64pos_dir0
// ISEL-DAG: WBARWUA 0,
// ISEL-DAG: WBARWUA 1,
// MIR-LABEL: name: ae0_sa64pos_dir0
// MIR: WBARWUA{{.*}}_AR
// MIR: WBARWUA{{.*}}_AR
void ae0_sa64pos_dir0(ae_valign align, void *ptr) {
  AE_SA64POS_FP(align, ptr);
}
