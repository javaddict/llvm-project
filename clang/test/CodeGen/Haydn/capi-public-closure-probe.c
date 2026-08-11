// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O2 -c -o %t.o2.o %s
// Negative: HaydnAeBuiltin PublicEnabled default-off — no haydn_* wrapper.
// RUN: not %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -fsyntax-only -DPROBE_AE_UNPUBLISHED %s
// REQUIRES: haydn-registered-target
//
// C0.5 / G-CAPI-CLOSURE public exit gate:
//   - Every enabled public family used below compiles C → IR → object at
//     -O0 and -O2 (no cannot-select / cannot compile builtin).
//   - HaydnAeBuiltin stays off the haydn.h surface (PublicEnabled=0).
//   - No FormatID / slot / AltDesc leak required in this TU (header law).
// Extends IR-only d208-full-coverage.c + C0.1–C0.4 family probes.
//
// Exhaustive scalar/SIMD builtin IR+object gate: d208-full-coverage.c (emit-obj).
// This file proves the *public* haydn.h surface for gated families + samples.

#ifdef PROBE_AE_UNPUBLISHED
#include <haydn.h>
/* Must fail: mul16js is HaydnAeBuiltin with PublicEnabled=0. */
long long ae_must_not_be_public(long long a) { return haydn_mul16js(a); }
#else

#include <haydn.h>

// Keep results live under -O2.
volatile long long sink_ll;
volatile int sink_i;
volatile haydn_x2int32 sink_v2;
volatile haydn_x4int16 sink_v4;

//===----------------------------------------------------------------------===//
// C0.1 — WITH_* side-effecting loads/stores (public haydn_* + builtins)
//===----------------------------------------------------------------------===//

// IR-LABEL: @pub_d_lw_with_imm
// IR: call i64 @llvm.haydn.d.lw.with.imm
long long pub_d_lw_with_imm(const void *base) {
  long long r = haydn_d_lw_with_imm(base, 0);
  sink_ll = r;
  return r;
}

// IR-LABEL: @pub_s_lw_with_imm
// IR: call i32 @llvm.haydn.s.lw.with.imm
int pub_s_lw_with_imm(const void *base) {
  int r = haydn_s_lw_with_imm(base, 4);
  sink_i = r;
  return r;
}

// IR-LABEL: @pub_d_sdw_with_imm
// IR: call void @llvm.haydn.d.sdw.with.imm
void pub_d_sdw_with_imm(long long data, void *base) {
  haydn_d_sdw_with_imm(data, base, 0);
}

// IR-LABEL: @pub_d_ldw_with_reg
// IR: call i64 @llvm.haydn.d.ldw.with.reg
long long pub_d_ldw_with_reg(const void *base, int off) {
  return haydn_d_ldw_with_reg(base, off);
}

// IR-LABEL: @pub_s_sw_with_reg
// IR: call void @llvm.haydn.s.sw.with.reg
void pub_s_sw_with_reg(int data, void *base, int off) {
  haydn_s_sw_with_reg(data, base, off);
}

//===----------------------------------------------------------------------===//
// C0.2 — POST/PRE stores (writeback int return)
//===----------------------------------------------------------------------===//

// IR-LABEL: @pub_d_sdw_post_imm
// IR: call ptr @llvm.haydn.d.sdw.post.imm
void *pub_d_sdw_post_imm(long long data, void *base) {
  return haydn_d_sdw_post_imm(data, base, 0);
}

// IR-LABEL: @pub_d_sdw_pre_imm
// IR: call ptr @llvm.haydn.d.sdw.pre.imm
void *pub_d_sdw_pre_imm(long long data, void *base) {
  return haydn_d_sdw_pre_imm(data, base, 1);
}

// IR-LABEL: @pub_s_sb_post_imm
// IR: call ptr @llvm.haydn.s.sb.post.imm
void *pub_s_sb_post_imm(int data, void *base) {
  return haydn_s_sb_post_imm(data, base, 0);
}

// IR-LABEL: @pub_s_sw_pre_reg
// IR: call ptr @llvm.haydn.s.sw.pre.reg
void *pub_s_sw_pre_reg(int data, void *base, int off) {
  return haydn_s_sw_pre_reg(data, base, off);
}

// IR-LABEL: @pub_d_sw_h_post_imm
// IR: call ptr @llvm.haydn.d.sw.h.post.imm
void *pub_d_sw_h_post_imm(long long data, void *base) {
  return haydn_d_sw_h_post_imm(data, base, 0);
}

//===----------------------------------------------------------------------===//
// C0.3 — composed complex-MAC frexp pairs + SoftISqrt isqrt
//===----------------------------------------------------------------------===//

// IR-LABEL: @pub_x2cmula32
// IR: call {{.*}}@llvm.haydn.x2cmul32(
// IR-NOT: llvm.haydn.x2cmula32
long long pub_x2cmula32(long long acc1, long long acc2, haydn_x2int32 a,
                        haydn_x2int32 b) {
  haydn_dpair_t p = haydn_x2cmula32(acc1, acc2, a, b);
  return p.hi ^ p.lo;
}

// IR-LABEL: @pub_x2cmuls32s
// IR: call {{.*}}@llvm.haydn.x2cmul32s(
// IR-NOT: llvm.haydn.x2cmuls32s
long long pub_x2cmuls32s(long long acc1, long long acc2, haydn_x2int32 a,
                         haydn_x2int32 b) {
  haydn_dpair_t p = haydn_x2cmuls32s(acc1, acc2, a, b);
  return p.hi ^ p.lo;
}

// IR-LABEL: @pub_isqrt
// IR-NOT: llvm.haydn.isqrt
int pub_isqrt(int a) {
  int r = haydn_isqrt(a);
  sink_i = r;
  return r;
}

//===----------------------------------------------------------------------===//
// C0.4 — ConstArg / ImmArg public macros (ICE at call site)
//===----------------------------------------------------------------------===//

// IR-LABEL: @pub_x2srai32
// IR: call {{.*}}@llvm.haydn.x2srai32
haydn_x2int32 pub_x2srai32(haydn_x2int32 a) {
  haydn_x2int32 r = haydn_x2srai32(a, 3);
  sink_v2 = r;
  return r;
}

// IR-LABEL: @pub_arctan
// IR: call {{.*}}@llvm.haydn.arctan
int pub_arctan(long long a) {
  return haydn_arctan(a, 2);
}

// IR-LABEL: @pub_setcbr_begin
// IR: call {{.*}}@llvm.haydn.setcbr
void pub_setcbr_begin(void) {
  /* ImmArg: both bounds must be ICE at the call site. */
  haydn_setcbr_begin(0, 64);
}

//===----------------------------------------------------------------------===//
// Representative enabled public surface (scalar / SIMD / pair / CB / BREV)
//===----------------------------------------------------------------------===//

// IR-LABEL: @pub_add64
// IR: call {{.*}}@llvm.haydn.add64
long long pub_add64(long long a, long long b) {
  return haydn_add64(a, b);
}

// IR-LABEL: @pub_x2add32s
// IR: call {{.*}}@llvm.haydn.x2add32s
haydn_x2int32 pub_x2add32s(haydn_x2int32 a, haydn_x2int32 b) {
  return haydn_x2add32s(a, b);
}

// IR-LABEL: @pub_x4add16s
// IR: call {{.*}}@llvm.haydn.x4add16s
haydn_x4int16 pub_x4add16s(haydn_x4int16 a, haydn_x4int16 b) {
  return haydn_x4add16s(a, b);
}

// IR-LABEL: @pub_mul64_ss_ll
// IR: call {{.*}}@llvm.haydn.mul64.ss.ll
long long pub_mul64_ss_ll(long long a, long long b) {
  return haydn_mul64_ss_ll(a, b);
}

// IR-LABEL: @pub_x2mul32_pair
// frexp → haydn_dpair_t
long long pub_x2mul32_pair(haydn_x2int32 a, haydn_x2int32 b) {
  haydn_dpair_t p = haydn_x2mul32(a, b);
  return p.hi ^ p.lo;
}

// Circular-buffer load frexp (public special)
// IR-LABEL: @pub_ldw_cb_imm
haydn_cb_ld_t pub_ldw_cb_imm(const void *base) {
  return haydn_ldw_cb_imm(base, 0, 0);
}

// BREV load frexp
// IR-LABEL: @pub_ldw_brev_imm
haydn_cb_ld_t pub_ldw_brev_imm(const void *base) {
  return haydn_ldw_brev_imm(base, 4);
}

// POST/PRE AGU writeback load frexp (D_* → haydn_cb_ld_t i64 data)
// IR-LABEL: @pub_d_lw_post_imm
haydn_cb_ld_t pub_d_lw_post_imm(const void *base) {
  return haydn_d_lw_post_imm(base, 0);
}

// Soft-move immediates (ImmArg). Two operands, not one: the database is
// `MOVEI_L rtd, imm32` with `rtd = {rtd[63:32], imm32}`, so rtd is READ as
// well as written — the instruction splices the immediate into one half and
// leaves the other alone. Building a value from nothing passes 0 for it.
// IR-LABEL: @pub_movei_l
// IR: call {{.*}}@llvm.haydn.movei
long long pub_movei_l(void) {
  return haydn_movei_l(0, 0x1234);
}

// Absolute / saturate samples
// IR-LABEL: @pub_abs64s
long long pub_abs64s(long long a) { return haydn_abs64s(a); }

// IR-LABEL: @pub_abs32s
int pub_abs32s(int a) { return haydn_abs32s(a); }

// Sin/cos ImmArg
// IR-LABEL: @pub_sin_cos
long long pub_sin_cos(int phase) { return haydn_sin_cos(phase, 1); }

// UA post vector wrappers (switch-literal ar_sel in haydn.h specials).
// Format E dropped the stride and the direction select (§ 8 Q1), so ar_sel is
// the only thing after the pointer.
// IR-LABEL: @pub_d_ltwua_post
haydn_x2int32 pub_d_ltwua_post(const void *ptr) {
  return haydn_d_ltwua_post(ptr, 0);
}

// IR-LABEL: @pub_d_stwua_post
void pub_d_stwua_post(haydn_x2int32 data, void *ptr) {
  haydn_d_stwua_post(data, ptr, 1);
}

// Golden D-ALU / bitwise samples
// IR-LABEL: @pub_sub64
long long pub_sub64(long long a, long long b) { return haydn_sub64(a, b); }

// IR-LABEL: @pub_xor64
long long pub_xor64(long long a, long long b) { return haydn_xor64(a, b); }

// IR-LABEL: @pub_and64
long long pub_and64(long long a, long long b) { return haydn_and64(a, b); }

// X4 lane select ImmArg
// IR-LABEL: @pub_x4seli16
haydn_x4int16 pub_x4seli16(haydn_x4int16 a, haydn_x4int16 b) {
  return haydn_x4seli16(a, b, 5);
}

// Exp2 / arctan already covered; brev32 reg form
// IR-LABEL: @pub_brev32
int pub_brev32(int a, int b) { return haydn_brev32(a, b); }

// IR-LABEL: @pub_exp2
int pub_exp2(int a) { return haydn_exp2(a); }

// Store WITH void forms
// IR-LABEL: @pub_d_shw_with_imm
void pub_d_shw_with_imm(long long data, void *base) {
  haydn_d_shw_with_imm(data, base, 0);
}

// IR-LABEL: @pub_s_shw_with_reg
void pub_s_shw_with_reg(int data, void *base, int off) {
  haydn_s_shw_with_reg(data, base, off);
}

// Additional POST stores for DR64 half/low coverage
// IR-LABEL: @pub_d_shw_post_reg
void *pub_d_shw_post_reg(long long data, void *base, int off) {
  return haydn_d_shw_post_reg(data, base, off);
}

// IR-LABEL: @pub_d_sw_l_pre_imm
void *pub_d_sw_l_pre_imm(long long data, void *base) {
  return haydn_d_sw_l_pre_imm(data, base, 2);
}

// GPR WITH loads
// IR-LABEL: @pub_s_lbs_with_imm
int pub_s_lbs_with_imm(const void *base) { return haydn_s_lbs_with_imm(base, 0); }

// IR-LABEL: @pub_s_lhwu_with_reg
int pub_s_lhwu_with_reg(const void *base, int off) {
  return haydn_s_lhwu_with_reg(base, off);
}

// Compose remaining complex MAC public wrappers
// IR-LABEL: @pub_x2cmula32s
long long pub_x2cmula32s(long long acc1, long long acc2, haydn_x2int32 a,
                         haydn_x2int32 b) {
  haydn_dpair_t p = haydn_x2cmula32s(acc1, acc2, a, b);
  return p.hi ^ p.lo;
}

// IR-LABEL: @pub_x2cmuls32
long long pub_x2cmuls32(long long acc1, long long acc2, haydn_x2int32 a,
                        haydn_x2int32 b) {
  haydn_dpair_t p = haydn_x2cmuls32(acc1, acc2, a, b);
  return p.hi ^ p.lo;
}

// Root aggregator so the TU has a non-empty external entry under -O2 LTO-ish
// DCE when only internals are analyzed; keeps sinks touched.
long long capi_public_closure_root(long long a, long long b, void *base,
                                   haydn_x2int32 v2, haydn_x4int16 v4) {
  sink_ll = pub_add64(a, b);
  sink_ll ^= pub_d_lw_with_imm(base);
  sink_i ^= (int)(long)pub_d_sdw_post_imm(a, base);
  sink_ll ^= pub_x2cmula32(a, b, v2, v2);
  sink_i ^= pub_isqrt((int)a);
  sink_v2 = pub_x2add32s(v2, v2);
  sink_v4 = pub_x4add16s(v4, v4);
  sink_i ^= pub_arctan(a);
  return sink_ll ^ (long long)sink_i;
}

#endif /* !PROBE_AE_UNPUBLISHED */
