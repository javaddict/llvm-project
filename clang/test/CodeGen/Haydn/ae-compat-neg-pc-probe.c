// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
//
// REQUIRES: haydn-registered-target
//
// AE_LA*NEG_PC is probe-only seed parity with POS_PC: both prime AR residual
// via PLDWWUA (haydn_ae_la64_pp). Reverse vs forward is the later step's
// dir ImmArg (RIC=1 / IC=0). Do not invent pre-decrement seed semantics.
// Contrast: AE_SA64NEG_FP store-finish owns dir ImmArg=1 (not a POS alias).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4NEG_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2NEG_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4POS_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2POS_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24POS_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA24X2POS_PC == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64POS_FP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA64NEG_FP == HAYDN_COMPAT_EMULATED, "");
/* Probe-only: NEG seed tier/body parity with POS (no reverse invent). */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA16X4NEG_PC ==
                   HAYDN_COMPAT_TIER_AE_LA16X4POS_PC,
               "NEG16 seed tier must equal POS16");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2NEG_PC ==
                   HAYDN_COMPAT_TIER_AE_LA32X2POS_PC,
               "NEG32 seed tier must equal POS32");
/* Dual-24 F24 POS seed shares base 32x2 POS PLDWWUA law. */
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24POS_PC ==
                   HAYDN_COMPAT_TIER_AE_LA32X2POS_PC,
               "F24 POS seed tier must equal base 32x2 POS");

// IR-LABEL: @neg16_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// IR-NOT: predec
void neg16_seeds_pldwwua(ae_valign *al, const ae_int16x4 *ptr) {
  AE_LA16X4NEG_PC(*al, ptr);
}

// IR-LABEL: @neg32_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// IR-NOT: predec
void neg32_seeds_pldwwua(ae_valign *al, const ae_int32x2 *ptr) {
  AE_LA32X2NEG_PC(*al, ptr);
}

// POS peer: same seed intrinsic (probe-only parity).
// IR-LABEL: @pos16_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
void pos16_seeds_pldwwua(ae_valign *al, const ae_int16x4 *ptr) {
  AE_LA16X4POS_PC(*al, ptr);
}

// IR-LABEL: @pos32_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
void pos32_seeds_pldwwua(ae_valign *al, const ae_int32x2 *ptr) {
  AE_LA32X2POS_PC(*al, ptr);
}

// Dual-24 F24 POS seed: same PLDWWUA path (no reverse invent).
// IR-LABEL: @f24pos_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// IR-NOT: predec
void f24pos_seeds_pldwwua(ae_valign *al, const ae_f24x2 *ptr) {
  AE_LA32X2F24POS_PC(*al, ptr);
}

// IR-LABEL: @f24pos_alias_seeds_pldwwua
// IR: call void @llvm.haydn.pldwwua
// IR-NOT: predec
void f24pos_alias_seeds_pldwwua(ae_valign *al, const ae_f24x2 *ptr) {
  AE_LA24X2POS_PC(*al, ptr);
}

// Store-finish residual: SA64POS dir=0 / SA64NEG dir=1 via WBARWUA.
// Unlike LA*NEG_PC seed, NEG finish must not silent-alias POS dir=0.
// IR-LABEL: @sa64pos_finish_dir0
// IR: call {{.*}}@llvm.haydn.wbarwua({{.*}}i32 0)
// IR-NOT: call {{.*}}@llvm.haydn.wbarwua({{.*}}i32 1)
void sa64pos_finish_dir0(ae_valign *al, void *ptr) {
  AE_SA64POS_FP(*al, ptr);
}

// IR-LABEL: @sa64neg_finish_dir1
// IR: call {{.*}}@llvm.haydn.wbarwua({{.*}}i32 1)
// IR-NOT: call {{.*}}@llvm.haydn.wbarwua({{.*}}i32 0)
void sa64neg_finish_dir1(ae_valign *al, void *ptr) {
  AE_SA64NEG_FP(*al, ptr);
}
