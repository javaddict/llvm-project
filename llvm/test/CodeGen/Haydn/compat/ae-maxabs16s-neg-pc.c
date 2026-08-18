// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR

// Role: semantic — Public AE compatibility value/ref probes:
//   AE_MAXABS16S  — quad-16 max-abs via X4ABS16S + X4MAX16 composite.
//                   Must NOT lower through haydn_maxabs32s (2x32).
//   AE_LA*NEG_PC  — PLDWWUA seed equal to POS_PC; reverse dir is on
//                   subsequent RIC/RIP ImmArg (not a silent direction erase).
//
// Bug being guarded (MAXABS16S): prior body routed 4x16 through
// haydn_maxabs32s, which reinterprets four 16-bit lanes as two 32-bit
// abs-max lanes. NatureDSP FFT bexp
// (fft_cplx16x16_hifi3.c:2132) uses per-lane
//   acc[i] = MAX(acc[i], SAT16(ABS(val[i])))
// then a separate AE_MAX16 horizontal tree. Returning ae_int16x4.
//
// What would break if the bug reappears: FFT bexp magnitude tracking
// silently wrong under two-lane abs; IR would show llvm.haydn.maxabs32s
// instead of x4abs16s/x4max16.
//
// Default fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_MAXABS16S == HAYDN_COMPAT_EMULATED,
               "MAXABS16S emulated composite");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4NEG_PC == HAYDN_COMPAT_EXACT,
               "NEG_PC exact PLDWWUA");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2NEG_PC == HAYDN_COMPAT_EXACT,
               "NEG_PC exact PLDWWUA");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED,
               "ADD64X2_ permanent unsupported");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64POS_FP == HAYDN_COMPAT_EMULATED,
               "SA64POS_FP store-finish dir0");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED,
               "SA64NEG_FP store-finish dir1");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// 2-arg: max(acc, sat_abs(val)) per 16-bit lane.
// IR-LABEL: @maxabs16s_2arg_x4
// IR-DAG: call {{.*}}@llvm.haydn.x4abs16s
// IR-DAG: call {{.*}}@llvm.haydn.x4max16
// IR-NOT: call {{.*}}@llvm.haydn.maxabs32s
// ASM-LABEL: maxabs16s_2arg_x4:
// ASM-DAG: x4abs16s
// ASM-DAG: x4max16
// ASM-NOT: maxabs32s
// ASM-NOT: x2abs32s
ae_int16x4 maxabs16s_2arg_x4(ae_int16x4 acc, ae_int16x4 val) {
  return AE_MAXABS16S(acc, val);
}

// 1-arg: per-lane sat-abs only (not horizontal max-abs).
// IR-LABEL: @maxabs16s_1arg_sat_abs
// IR: call {{.*}}@llvm.haydn.x4abs16s
// IR-NOT: call {{.*}}@llvm.haydn.maxabs32s
// IR-NOT: call {{.*}}@llvm.haydn.x4hmax16
// ASM-LABEL: maxabs16s_1arg_sat_abs:
// ASM: x4abs16s
// ASM-NOT: maxabs32s
// ASM-NOT: x4hmax16
ae_int16x4 maxabs16s_1arg_sat_abs(ae_int16x4 a) {
  return AE_MAXABS16S(a);
}

// NEG_PC seeds AR residual via PLDWWUA (same path as POS_PC).
// IR-LABEL: @la32x2neg_pc_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// ASM-LABEL: la32x2neg_pc_seeds_pldwwua:
// ASM: pldwwua
void la32x2neg_pc_seeds_pldwwua(ae_valign *al, const ae_int32x2 *ptr) {
  AE_LA32X2NEG_PC(*al, ptr);
}

// IR-LABEL: @la16x4neg_pc_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// ASM-LABEL: la16x4neg_pc_seeds_pldwwua:
// ASM: pldwwua
void la16x4neg_pc_seeds_pldwwua(ae_valign *al, const ae_int16x4 *ptr) {
  AE_LA16X4NEG_PC(*al, ptr);
}

// POS peer for comparison — same seed intrinsic.
// IR-LABEL: @la32x2pos_pc_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// ASM-LABEL: la32x2pos_pc_seeds_pldwwua:
// ASM: pldwwua
void la32x2pos_pc_seeds_pldwwua(ae_valign *al, const ae_int32x2 *ptr) {
  AE_LA32X2POS_PC(*al, ptr);
}

// Dual-24 F24 POS seed: same PLDWWUA path as base POS (probe-only law).
// IR-LABEL: @la32x2f24pos_pc_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// IR-NOT: predec
// ASM-LABEL: la32x2f24pos_pc_seeds_pldwwua:
// ASM: pldwwua
// ASM-NOT: predec
void la32x2f24pos_pc_seeds_pldwwua(ae_valign *al, const ae_f24x2 *ptr) {
  AE_LA32X2F24POS_PC(*al, ptr);
}
