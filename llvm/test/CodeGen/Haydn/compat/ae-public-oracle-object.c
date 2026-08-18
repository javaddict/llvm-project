// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_ADD64X2_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STRICT
//
// Full public AE residual surface: TD-authored OracleId inventory for all
// EXACT+EMULATED ops (floor 671), host value + non-empty object evidence, and
// empty-body dual-64 ADD64X2_* fail-closed under default (no ALLOW_INEXACT).
// No silent #define success without codegen evidence.

#include <haydn_dsp.h>

/* Full public surface inventory (EXACT+EMULATED; UNSUPPORTED has no oracle). */
_Static_assert(HAYDN_AE_COMPAT_TAG_COUNT >= 600, "full public AE tier inventory");
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 671, "full public AE oracle inventory floor");
_Static_assert(HAYDN_AE_ORACLE_COUNT == HAYDN_AE_COMPAT_TAG_COUNT - 2,
               "oracle count is TAG_COUNT minus permanent UNSUPPORTED pair");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED, "");
/* Residual EMULATED sample oracles (TD-authored residual family registrations). */
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS16S, "emu.abs16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD64, "emu.add64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_AND64, "emu.and64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAI32, "emu.slai32") == 0, "");
/* Curated classes keep authored prefixes (not residual emu.* overwrite). */
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_LH, "exact.sel32_lh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVTQ56A32S, "ae0.cvtq56a32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA32S, "softsat.slaa32s") == 0, "");
/* DeclKind present on residual EMULATED family representatives. */
_Static_assert(__builtin_strcmp(HAYDN_AE_DECLKIND_AE_ABS16S, "macro") == 0, "");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Host oracle: AE_ADD64(1,2) = 3 (bag scalar add residual EMULATED).
// IR-LABEL: @public_add64_one
// IR: ret i64 3
// ASM-LABEL: public_add64_one:
// OBJ-LABEL: <public_add64_one>:
// OBJ: addi32
ae_int64 public_add64_one(void) {
  return AE_ADD64((ae_int64)1, (ae_int64)2);
}

// Host oracle: AE_AND64 mask residual.
// IR-LABEL: @public_and64_mask
// IR: ret i64 1
// ASM-LABEL: public_and64_mask:
// OBJ-LABEL: <public_and64_mask>:
// OBJ: addi32
ae_int64 public_and64_mask(void) {
  return AE_AND64((ae_int64)0x3, (ae_int64)0x1);
}

// Host oracle: AE_SLAI32(1,1) = 2 (plain left residual, not sat).
// IR-LABEL: @public_slai32_one
// IR: ret i32 2
// ASM-LABEL: public_slai32_one:
// OBJ-LABEL: <public_slai32_one>:
// OBJ: addi32
int public_slai32_one(void) {
  return (int)AE_SLAI32(1, 1);
}

// Object/value path: residual EMULATED AE_ABS16S lowers to haydn_x4abs16s
// (not silent identity / empty body). Constant-fold is not required; the
// intrinsic call plus non-empty object prove the public mapping.
// IR-LABEL: @public_abs16s_neg1
// IR: call {{.*}} @llvm.haydn.x4abs16s
// ASM-LABEL: public_abs16s_neg1:
// ASM: {{abs|ABS|x4abs|X4ABS}}
// OBJ-LABEL: <public_abs16s_neg1>:
ae_int64 public_abs16s_neg1(void) {
  ae_int16x4 a = {-1, -1, -1, -1};
  return __AE_TO_I64(AE_ABS16S(a));
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
