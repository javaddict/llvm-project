// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -ffreestanding %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O0 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// D1.15: AE 32x2 reverse-load lane law. Golden adjudication
// (instruction_type_index.json type AR): D_LTWUA_POST / D_STWUA_POST have
// NO direction operand — `temp=mem64[rs&~7]; window={temp,ar};
// rtd=(rs[2]==0)?window[63:00]:window[95:32]; ar=temp; rs=rs+8` and
// `rs[2]==0 → mem64=rtd; rs[2]==1 → mem64={rtd[31:00],ar[31:00]},
// ar[31:00]=rtd[63:32]` — direction only steps the pointer; data word
// order is direction-independent. Therefore the RIP family owes the SAME
// H-first presentation as AE_LA32X2_IP/IC/RIC: the raw LE window goes
// through haydn_ae_f32x2_mem_to_reg, and stores swap src before the UA
// step (golden store concat puts rtd[31:0] first). Value-level executed
// round-trip: BundleSim case ae_rip32x2_roundtrip.

#include <haydn_dsp.h>

_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2_RIP EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIP EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2_RIP EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_SA32X2F24_RIP == HAYDN_COMPAT_EXACT,
               "SA32X2F24_RIP EXACT");
_Static_assert(HAYDN_COMPAT_TIER_AE_LA32X2F24_RIC == HAYDN_COMPAT_EXACT,
               "LA32X2F24_RIC EXACT");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "default fail-closed");

// IR-LABEL: @la32x2_rip_3arg
// Reverse UA load dir=1 ImmArg, then the lane swap.
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2_rip_3arg
// ASM: d_ltwua_post
ae_int32x2 la32x2_rip_3arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIP(d, al, p);
  return d;
}

// IR-LABEL: @la32x2_rip_4arg
// 4-arg arity keeps dir=1 and adds the swap.
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2_rip_4arg
// ASM: d_ltwua_post
ae_int32x2 la32x2_rip_4arg(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIP(d, al, p, 8);
  return d;
}

// IR-LABEL: @la32x2f24_rip_4arg
// Dual-24 reverse UA load: same dir=1 + swap, keeps the (ae_f24x2) cast.
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2f24_rip_4arg
// ASM: d_ltwua_post
ae_f24x2 la32x2f24_rip_4arg(ae_f24x2 *p) {
  ae_f24x2 d = (ae_f24x2)0;
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2F24_RIP(d, al, p, 8);
  return d;
}

// O0-LABEL: @sa32x2_rip_lane
// Reverse UA store dir=1: src is swapped BEFORE the UA step (O0 shows the
// concrete call order around haydn_ae_sa64_step).
// O0: call {{.*}}@haydn_ae_f32x2_mem_to_reg
// O0: call {{.*}}@haydn_ae_sa64_step({{.*}}i32 noundef 8, i32 noundef 1)
// IR-LABEL: @sa32x2_rip_lane
// Constant src folds through the swap: {1,2} bag 0x0000000200000001 -> swapped
// 0x0000000100000002 = 4294967298 — proves the store applies mem_to_reg.
// IR: call {{.*}}@llvm.haydn.d.stwua.post(i64 4294967298,{{.*}}i32 {{[0-3]}}, i32 8, i32 1)
// ASM-LABEL: sa32x2_rip_lane
// ASM: d_stwua_post
ae_int32x2 *sa32x2_rip_lane(ae_int32x2 *p) {
  ae_int32x2 v = {1, 2};
  ae_valign al = AE_ZALIGN64();
  AE_SA32X2_RIP(v, al, p);
  return p;
}

// O0-LABEL: @sa32x2f24_rip_lane
// Dual-24 reverse UA store: swap before the UA step, dir=1.
// O0: call {{.*}}@haydn_ae_f32x2_mem_to_reg
// O0: call {{.*}}@haydn_ae_sa64_step({{.*}}i32 noundef 8, i32 noundef 1)
// ASM-LABEL: sa32x2f24_rip_lane
// ASM: d_stwua_post
ae_f24x2 *sa32x2f24_rip_lane(ae_f24x2 *p) {
  ae_f24x2 v = (ae_f24x2)0;
  ae_valign al = AE_ZALIGN64();
  AE_SA32X2F24_RIP(v, al, p);
  return p;
}

// Contrast: forward IP is dir=0 — the RIP path must not silent-alias it.
// IR-LABEL: @la32x2_ip_forward_contrast
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 0)
// ASM-LABEL: la32x2_ip_forward_contrast
// ASM: d_ltwua_post
ae_int32x2 la32x2_ip_forward_contrast(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_IP(d, al, p);
  return d;
}

// RIC peer stays green: dir=1 + cbr wrap -8 + swap (unchanged by D1.15).
// IR-LABEL: @la32x2_ric_peer
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2_ric_peer
// ASM: d_ltwua_post
ae_int32x2 la32x2_ric_peer(ae_int32x2 *p) {
  ae_int32x2 d = {0};
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2_RIC(d, al, p, 0);
  return d;
}

// LA32X2F24_RIC joined the same lane law (D1.15 fold: its comment already
// claimed "same path as AE_LA32X2_RIC", which swaps).
// IR-LABEL: @la32x2f24_ric_lane
// IR: call {{.*}}@llvm.haydn.d.ltwua.post(ptr {{[^,]+}}, i32 {{[0-3]}}, i32 8, i32 1)
// IR: call {{.*}}@llvm.fshl.i64({{.*}}i64 32)
// ASM-LABEL: la32x2f24_ric_lane
// ASM: d_ltwua_post
ae_f24x2 la32x2f24_ric_lane(ae_f24x2 *p) {
  ae_f24x2 d = (ae_f24x2)0;
  ae_valign al = AE_ZALIGN64();
  AE_LA32X2F24_RIC(d, al, p, 0);
  return d;
}
