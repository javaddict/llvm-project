// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -c -o %t.o %s
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
//
// EXACT public surface residual class: value + object oracles.
// Empty output fails. Default fail-closed (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_LH == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_HH == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SEL32_LL == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO24 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD24S == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG24S == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADDP24 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4_RIP == HAYDN_COMPAT_EXACT, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_LH, "exact.sel32_lh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_HH, "exact.sel32_hh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_LL, "exact.sel32_ll") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ZERO24, "exact.zero24") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD24S, "exact.add24s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NEG24S, "exact.neg24s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADDP24, "exact.addp24") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_LA16X4_RIP, "exact.la16x4_rip") == 0, "");
// EXACT(70) + AE-P0 + softsat residual inventory floor.
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 671, "full public AE oracle inventory floor");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// IR-LABEL: @exact_sel32_lh
// IR: call {{.*}}@llvm.haydn.x2sel32.lh
// ASM-LABEL: exact_sel32_lh:
// ASM: x2sel32_lh
// OBJ-LABEL: <exact_sel32_lh>:
// OBJ: x2sel32_lh
ae_int32x2 exact_sel32_lh(ae_int32x2 a, ae_int32x2 b) {
  return AE_SEL32_LH(a, b);
}

// IR-LABEL: @exact_sel32_hh
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// ASM-LABEL: exact_sel32_hh:
// ASM: x2sel32_hh
// OBJ-LABEL: <exact_sel32_hh>:
// OBJ: x2sel32_hh
ae_int32x2 exact_sel32_hh(ae_int32x2 a, ae_int32x2 b) {
  return AE_SEL32_HH(a, b);
}

// IR-LABEL: @exact_sel32_ll
// IR: call {{.*}}@llvm.haydn.x2sel32.ll
// ASM-LABEL: exact_sel32_ll:
// ASM: x2sel32_ll
// OBJ-LABEL: <exact_sel32_ll>:
// OBJ: x2sel32_ll
ae_int32x2 exact_sel32_ll(ae_int32x2 a, ae_int32x2 b) {
  return AE_SEL32_LL(a, b);
}

// Host oracle: ZERO24 is the dual-24 zero constant (bag 0).
// IR-LABEL: @exact_zero24
// IR: ret {{.*}}0
// ASM-LABEL: exact_zero24:
// OBJ-LABEL: <exact_zero24>:
// OBJ: addi32
ae_f24x2 exact_zero24(void) {
  return AE_ZERO24();
}

// IR-LABEL: @exact_add24s
// IR: call {{.*}}@llvm.haydn.x2add32s
// ASM-LABEL: exact_add24s:
// ASM: x2add32s
// OBJ-LABEL: <exact_add24s>:
// OBJ: x2add32s
ae_f24x2 exact_add24s(ae_f24x2 a, ae_f24x2 b) {
  return AE_ADD24S(a, b);
}

// IR-LABEL: @exact_neg24s
// IR: call {{.*}}@llvm.haydn.x2neg32s
// ASM-LABEL: exact_neg24s:
// ASM: x2neg32s
// OBJ-LABEL: <exact_neg24s>:
// OBJ: x2neg32s
ae_f24x2 exact_neg24s(ae_f24x2 a) {
  return AE_NEG24S(a);
}

// IR-LABEL: @exact_addp24
// IR: call {{.*}}@llvm.haydn.x2add32
// ASM-LABEL: exact_addp24:
// ASM: x2add32
// OBJ-LABEL: <exact_addp24>:
// OBJ: x2add32
ae_f24x2 exact_addp24(ae_f24x2 a, ae_f24x2 b) {
  return AE_ADDP24(a, b);
}
