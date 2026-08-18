// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_ADD64X2_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STRICT
//
// Soft-sat residual public AE surface: host value + non-empty object oracles.
// Empty-body dual-64 ADD64X2_* stay fail-closed under default (no ALLOW_INEXACT).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI24S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAIS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA32S, "softsat.slaa32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAI32S, "softsat.slaa32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAS32S, "softsat.slaa32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAI24S, "softsat.slai24s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA16S, "softsat.slaa16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_F64_SLAIS, "softsat.f64_slais") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_F32X2_SLAIS, "softsat.f32x2_slais") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_F64_SLAS, "softsat.f64_slais") == 0, "");
// EXACT(70) + AE-P0(8 unique ids) + softsat residual inventory floor.
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 671, "full public AE oracle inventory floor");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Host oracle: dual-32 1<<1 per lane → bag 0x0000000200000002 = 8589934594.
// IR-LABEL: @softsat_slaa32s_one
// IR: ret i64 8589934594
// ASM-LABEL: softsat_slaa32s_one:
// OBJ-LABEL: <softsat_slaa32s_one>:
// OBJ: addi32
ae_int64 softsat_slaa32s_one(void) {
  ae_int32x2 a = {1, 1};
  return __AE_TO_I64(AE_SLAA32S(a, 1));
}

// Host oracle: dual-24 soft sat left peer of SLAA32S.
// IR-LABEL: @softsat_slai24s_one
// IR: ret i64 8589934594
// ASM-LABEL: softsat_slai24s_one:
// OBJ-LABEL: <softsat_slai24s_one>:
// OBJ: addi32
ae_int64 softsat_slai24s_one(void) {
  ae_f24x2 a = (ae_f24x2)__haydn_i64_as_v2(0x0000000100000001LL);
  return __AE_TO_I64(AE_SLAI24S(a, 1));
}

// Host oracle: ambient SAR=1 drives 1-arg SLAS32S (was silent identity).
// IR-LABEL: @softsat_slas32s_sar
// IR: ret i64 8589934594
// ASM-LABEL: softsat_slas32s_sar:
// OBJ-LABEL: <softsat_slas32s_sar>:
// OBJ: addi32
ae_int64 softsat_slas32s_sar(void) {
  WUR_AE_SAR(1);
  ae_int32x2 a = {1, 1};
  return __AE_TO_I64(AE_SLAS32S(a));
}

// Host oracle: F64_SLAIS 1<<1 = 2 (not plain wrap alias).
// IR-LABEL: @softsat_f64_slais_one
// IR: ret i64 2
// ASM-LABEL: softsat_f64_slais_one:
// ASM: addi32{{.*}}2
// OBJ-LABEL: <softsat_f64_slais_one>:
// OBJ: addi32
ae_int64 softsat_f64_slais_one(void) {
  return (ae_int64)AE_F64_SLAIS(1, 1);
}

// Host oracle: 0x4000... << 1 saturates to INT64_MAX.
// IR-LABEL: @softsat_f64_slais_sat_max
// IR: ret i64 9223372036854775807
// ASM-LABEL: softsat_f64_slais_sat_max:
// OBJ-LABEL: <softsat_f64_slais_sat_max>:
// OBJ: addi32
ae_int64 softsat_f64_slais_sat_max(void) {
  return (ae_int64)AE_F64_SLAIS((ae_f64)0x4000000000000000LL, 1);
}

// Host oracle: F64_SLAS alias of soft sat left.
// IR-LABEL: @softsat_f64_slas_one
// IR: ret i64 2
// ASM-LABEL: softsat_f64_slas_one:
// OBJ-LABEL: <softsat_f64_slas_one>:
// OBJ: addi32
ae_int64 softsat_f64_slas_one(void) {
  return (ae_int64)AE_F64_SLAS(1, 1);
}

// Host oracle: dual F32 soft sat left 1<<1 per lane.
// IR-LABEL: @softsat_f32x2_slais_one
// IR: ret i64 8589934594
// ASM-LABEL: softsat_f32x2_slais_one:
// OBJ-LABEL: <softsat_f32x2_slais_one>:
// OBJ: addi32
ae_int64 softsat_f32x2_slais_one(void) {
  ae_f32x2 a = (ae_f32x2)__haydn_i64_as_v2(0x0000000100000001LL);
  return __AE_TO_I64(AE_F32X2_SLAIS(a, 1));
}

// Host oracle: quad-16 1<<1 per lane → bag 0x0002000200020002.
// IR-LABEL: @softsat_slaa16s_one
// IR: ret i64 562958543486978
// ASM-LABEL: softsat_slaa16s_one:
// OBJ-LABEL: <softsat_slaa16s_one>:
// OBJ: addi32
ae_int64 softsat_slaa16s_one(void) {
  ae_int16x4 a = {1, 1, 1, 1};
  return __AE_TO_I64(AE_SLAA16S(a, 1));
}

#if defined(TEST_ADD64X2_STRICT)
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_
ae_int64 empty_body_add64x2_(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_(a, b);
}
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_vector
ae_int64 empty_body_add64x2_vector(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_vector(a, b);
}
#endif
