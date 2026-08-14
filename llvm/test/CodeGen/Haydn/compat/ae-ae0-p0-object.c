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
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -mllvm -stop-after=haydn-verify-bundles -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=MIR
//
// AE-P0 public surface: value + object oracles (empty output fails).
//   CVTQ56A32S / SLAA64S / TRUNCA32X2F64S — host value oracles
//   CVT16X4 1-arg — non-empty x4sat32t16 object path
//   SA64NEG vs SA64POS — dir ImmArg 1 vs 0 survives IR and committed MIR
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

// Host oracle: (int32_t)1 << 16 = 65536.
// IR-LABEL: @ae0_cvtq56_pos
// IR: ret i64 65536
// ASM-LABEL: ae0_cvtq56_pos:
// ASM: addi32{{.*}}65536
// OBJ-LABEL: <ae0_cvtq56_pos>:
// OBJ: addi32
ae_int64 ae0_cvtq56_pos(void) {
  return AE_CVTQ56A32S(1);
}

// Host oracle: (int32_t)-1 << 16 = -65536.
// IR-LABEL: @ae0_cvtq56_neg
// IR: ret i64 -65536
// ASM-LABEL: ae0_cvtq56_neg:
// ASM: addi32{{.*}}-65536
// OBJ-LABEL: <ae0_cvtq56_neg>:
// OBJ: addi32
ae_int64 ae0_cvtq56_neg(void) {
  return AE_CVTQ56A32S(-1);
}

// Host oracle: ordinary sat left 1<<1 = 2.
// IR-LABEL: @ae0_slaa64s_one
// IR: ret i64 2
// ASM-LABEL: ae0_slaa64s_one:
// ASM: addi32{{.*}}2
// OBJ-LABEL: <ae0_slaa64s_one>:
// OBJ: addi32
ae_int64 ae0_slaa64s_one(void) {
  return AE_SLAA64S((ae_int64)1, 1);
}

// Host oracle: 0x4000... << 1 saturates to INT64_MAX.
// IR-LABEL: @ae0_slaa64s_sat_max
// IR: ret i64 9223372036854775807
// ASM-LABEL: ae0_slaa64s_sat_max:
// OBJ-LABEL: <ae0_slaa64s_sat_max>:
// OBJ: addi32
ae_int64 ae0_slaa64s_sat_max(void) {
  return AE_SLAA64S((ae_int64)0x4000000000000000LL, 1);
}

// TRUNCA negative high lane: unsigned pack preserves hi bits → 5, -1.
// IR-LABEL: @ae0_trunca_pack
// IR: ret <2 x i32> <i32 5, i32 -1>
// ASM-LABEL: ae0_trunca_pack:
// ASM-DAG: addi32{{.*}}5
// ASM-DAG: addi32{{.*}}-1
// OBJ-LABEL: <ae0_trunca_pack>:
// OBJ: addi32
ae_int32x2 ae0_trunca_pack(void) {
  return AE_TRUNCA32X2F64S((ae_int64)5, (ae_int64)-1, 0);
}

// One-arg CVT16X4 → two-operand x4sat32t16 (zero pad); non-empty object.
// IR-LABEL: @ae0_cvt16x4_1arg
// IR: call {{.*}}@llvm.haydn.x4sat32t16
// ASM-LABEL: ae0_cvt16x4_1arg:
// ASM: x4sat32t16
// OBJ-LABEL: <ae0_cvt16x4_1arg>:
// OBJ: x4sat32t16
ae_int16x4 ae0_cvt16x4_1arg(ae_int32x2 a) {
  return AE_CVT16X4(a);
}

// SA64NEG_FP owns dir ImmArg 1 (must not silent-alias POS dir0) through
// IR and committed MIR. Object mnemonics may fuse ar_sel into the name;
// direction is pinned at IR + post-verify MIR (committed member operands).
// IR-LABEL: @ae0_sa64neg_dir1
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 1)
// ASM-LABEL: ae0_sa64neg_dir1:
// ASM: wbarwua
// OBJ-LABEL: <ae0_sa64neg_dir1>:
// OBJ: wbarwua
// MIR-LABEL: name: ae0_sa64neg_dir1
// MIR-DAG: WBARWUA{{[^,]*}}, 0, 1,
// MIR-DAG: WBARWUA{{[^,]*}}, 1, 1,
void ae0_sa64neg_dir1(ae_valign align, void *ptr) {
  AE_SA64NEG_FP(align, ptr);
}

// SA64POS_FP contrast: dir ImmArg 0.
// IR-LABEL: @ae0_sa64pos_dir0
// IR: call void @llvm.haydn.wbarwua(i32 {{.*}}, ptr {{.*}}, i32 0)
// ASM-LABEL: ae0_sa64pos_dir0:
// ASM: wbarwua
// OBJ-LABEL: <ae0_sa64pos_dir0>:
// OBJ: wbarwua
// MIR-LABEL: name: ae0_sa64pos_dir0
// MIR-DAG: WBARWUA{{[^,]*}}, 0, 0,
// MIR-DAG: WBARWUA{{[^,]*}}, 1, 0,
void ae0_sa64pos_dir0(ae_valign align, void *ptr) {
  AE_SA64POS_FP(align, ptr);
}
