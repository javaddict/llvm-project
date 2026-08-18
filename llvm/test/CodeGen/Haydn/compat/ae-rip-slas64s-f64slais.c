// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR

// Role: semantic — Public AE compatibility value/ref probes for residual reverse-RIP and saturating left-shift maps:.

// Public AE compatibility value/ref probes for residual reverse-RIP and
// saturating left-shift maps:
//   AE_S32X2_RIP / AE_S32X2F24_RIP / AE_L32X2F24_RIP
//       — reverse linear post-inc (ptr -= step), not forward IP
//   AE_SLAS64S — soft sat left by explicit shift or ambient AE_SAR
//       (not silent ASR / identity late-overload body)
//   AE_F64_SLAIS — soft sat left (not plain C << wrap)
//   AE_SLAS32S — dual-32 bidirectional sat (not always-right X2SRA32)
//   AE_F32X2_SLAIS — dual-32 sat left (not wrap X2SLL)
//   AE_SRAS32 — dual-32 ASR by ambient SAR (not scalar (x+0x8000)>>1)
//   AE_SLAS32 — dual-32 left by ambient SAR (non-sat; not SLAS32S)
//
// Default fail-closed mode (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_RIP == HAYDN_COMPAT_EXACT, "S32X2_RIP");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "S32X2F24_RIP");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "L32X2F24_RIP");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS64S == HAYDN_COMPAT_EMULATED, "SLAS64S");
_Static_assert(HAYDN_COMPAT_TIER_AE_F64_SLAIS == HAYDN_COMPAT_EMULATED,
               "F64_SLAIS");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED, "SLAS32S");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED,
               "F32X2_SLAIS");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAS32 == HAYDN_COMPAT_EXACT, "SRAS32");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32 == HAYDN_COMPAT_EXACT, "SLAS32");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRA64_32 == HAYDN_COMPAT_EMULATED,
               "SRA64_32");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIC");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");

// Reverse dual-32 store: 2-arg form steps ptr by -8 bytes (not +8).
// IR-LABEL: @s32x2_rip_rev8
// IR: getelementptr{{.*}}i32 -8
// IR-NOT: getelementptr{{.*}}i32 8
// ASM-LABEL: s32x2_rip_rev8:
ae_int32x2 *s32x2_rip_rev8(ae_int32x2 *p, ae_int32x2 v) {
  AE_S32X2_RIP(v, p);
  return p;
}

// Reverse dual-32 store with explicit byte step 16 → ptr -= 16.
// IR-LABEL: @s32x2_rip_rev16
// IR: getelementptr{{.*}}i32 -16
// ASM-LABEL: s32x2_rip_rev16:
ae_int32x2 *s32x2_rip_rev16(ae_int32x2 *p, ae_int32x2 v) {
  AE_S32X2_RIP(v, p, 16);
  return p;
}

// F24 reverse store same -8 contract.
// IR-LABEL: @s32x2f24_rip_rev8
// IR: getelementptr{{.*}}i32 -8
// ASM-LABEL: s32x2f24_rip_rev8:
ae_f24x2 *s32x2f24_rip_rev8(ae_f24x2 *p, ae_f24x2 v) {
  AE_S32X2F24_RIP(v, p);
  return p;
}

// F24 reverse load: read then ptr -= 8.
// IR-LABEL: @l32x2f24_rip_rev8
// IR: getelementptr{{.*}}i32 -8
// ASM-LABEL: l32x2f24_rip_rev8:
ae_f24x2 *l32x2f24_rip_rev8(ae_f24x2 *p) {
  ae_f24x2 d = 0;
  AE_L32X2F24_RIP(d, p);
  (void)d;
  return p;
}

// Forward IP contrast keeps positive +8 byte step on the returned cursor.
// IR-LABEL: @s32x2_ip_fwd8
// IR: getelementptr{{.*}}i32 8
// ASM-LABEL: s32x2_ip_fwd8:
ae_int32x2 *s32x2_ip_fwd8(ae_int32x2 *p, ae_int32x2 v) {
  AE_S32X2_IP(v, p, 8);
  return p;
}

// Host oracle: AE_SLAS64S(0x4000000000000000LL, 1) saturates to INT64_MAX
// (plain << wraps to INT64_MIN; silent ASR would yield 0x2000...).
#define HAYDN_ORACLE_SLAS64S_SAT ((ae_int64)0x7FFFFFFFFFFFFFFFLL)

// IR-LABEL: @slas64s_explicit_saturates
// IR: ret i64 9223372036854775807
// ASM-LABEL: slas64s_explicit_saturates:
ae_int64 slas64s_explicit_saturates(void) {
  ae_int64 r = AE_SLAS64S((ae_int64)0x4000000000000000LL, 1);
  return r ^ HAYDN_ORACLE_SLAS64S_SAT ^ HAYDN_ORACLE_SLAS64S_SAT;
}

// 1-arg form uses ambient AE_SAR (WUR_AE_SAR), not identity / ASR.
// IR-LABEL: @slas64s_sar_saturates
// IR: ret i64 9223372036854775807
// ASM-LABEL: slas64s_sar_saturates:
ae_int64 slas64s_sar_saturates(void) {
  WUR_AE_SAR(1);
  ae_int64 r = AE_SLAS64S((ae_int64)0x4000000000000000LL);
  return r ^ HAYDN_ORACLE_SLAS64S_SAT ^ HAYDN_ORACLE_SLAS64S_SAT;
}

// AE_F64_SLAIS same soft sat model as SLAI64S / F64_SLAS.
// IR-LABEL: @f64_slais_saturates
// IR: ret i64 9223372036854775807
// ASM-LABEL: f64_slais_saturates:
ae_int64 f64_slais_saturates(void) {
  ae_f64 r = AE_F64_SLAIS((ae_f64)0x4000000000000000LL, 1);
  ae_int64 v = (ae_int64)r;
  return v ^ HAYDN_ORACLE_SLAS64S_SAT ^ HAYDN_ORACLE_SLAS64S_SAT;
}

// Host oracle: AE_SLAS32S of dual (0x40000000, 0x40000000) << 1 saturates
// each lane to INT32_MAX. Always-right X2SRA32 would yield 0x20000000 each.
// Vector form: splat i32 2147483647 (not wrap to INT32_MIN / not ASR).
// IR-LABEL: @slas32s_sat_left_known
// IR: ret <2 x i32> {{.*}}2147483647
// ASM-LABEL: slas32s_sat_left_known:
ae_int32x2 slas32s_sat_left_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  return AE_SLAS32S(a, 1);
}

// 1-arg form uses ambient SAR: WUR_AE_SAR(1) then sat-left (not ASR by 1).
// IR-LABEL: @slas32s_sar_sat_left
// IR: ret <2 x i32> {{.*}}2147483647
// ASM-LABEL: slas32s_sar_sat_left:
ae_int32x2 slas32s_sar_sat_left(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  WUR_AE_SAR(1);
  return AE_SLAS32S(a);
}

// Negative shift: ASR each lane by 1 → 0x20000000 = 536870912 dual.
// IR-LABEL: @slas32s_asr_known
// IR: ret <2 x i32> {{.*}}536870912
// ASM-LABEL: slas32s_asr_known:
ae_int32x2 slas32s_asr_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  return AE_SLAS32S(a, -1);
}

// AE_F32X2_SLAIS same dual sat left (not wrap X2SLL to INT32_MIN).
// IR-LABEL: @f32x2_slais_sat_known
// IR: ret <2 x i32> {{.*}}2147483647
// ASM-LABEL: f32x2_slais_sat_known:
ae_f32x2 f32x2_slais_sat_known(void) {
  ae_f32x2 a = (ae_f32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  return AE_F32X2_SLAIS(a, 1);
}

// AE_SRAS32 1-arg: ambient SAR ASR both lanes (FFT scaling path).
// Input dual 0x40000000; amount 1. Silent late body ((x+0x8000)>>1) would
// emit scalar add/ashr and never call x2sra32 on a pair.
// IR-LABEL: @sras32_sar_asr_known
// IR: call {{.*}}@llvm.haydn.x2sra32(
// IR-SAME: splat (i32 1073741824)
// IR-SAME: i32 1
// IR-NOT: add {{.*}}32768
// ASM-LABEL: sras32_sar_asr_known:
// ASM: x2sra32
ae_int32x2 sras32_sar_asr_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  WUR_AE_SAR(1);
  return AE_SRAS32(a);
}

// AE_SRAS32 2-arg explicit amount — same dual X2SRA32.
// IR-LABEL: @sras32_explicit_asr_known
// IR: call {{.*}}@llvm.haydn.x2sra32(
// IR-SAME: splat (i32 1073741824)
// IR-SAME: i32 1
// ASM-LABEL: sras32_explicit_asr_known:
// ASM: x2sra32
ae_int32x2 sras32_explicit_asr_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x4000000040000000ULL);
  return AE_SRAS32(a, 1);
}

// AE_SLAS32 1-arg: ambient SAR logical left both lanes (non-sat wrap).
// Input dual 0x20000000; amount 1 → dual X2SLL32 (not sat SLAS32S path).
// IR-LABEL: @slas32_sar_sll_known
// IR: call {{.*}}@llvm.haydn.x2sll32(
// IR-SAME: splat (i32 536870912)
// IR-SAME: i32 1
// ASM-LABEL: slas32_sar_sll_known:
// ASM: x2sll32
ae_int32x2 slas32_sar_sll_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x2000000020000000ULL);
  WUR_AE_SAR(1);
  return AE_SLAS32(a);
}

// AE_SLAS32 2-arg explicit — same dual X2SLL32.
// IR-LABEL: @slas32_explicit_sll_known
// IR: call {{.*}}@llvm.haydn.x2sll32(
// IR-SAME: splat (i32 536870912)
// IR-SAME: i32 1
// ASM-LABEL: slas32_explicit_sll_known:
// ASM: x2sll32
ae_int32x2 slas32_explicit_sll_known(void) {
  ae_int32x2 a = (ae_int32x2)__haydn_i64_as_v2((haydn_dr64_t)0x2000000020000000ULL);
  return AE_SLAS32(a, 1);
}
