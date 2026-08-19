// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -emit-llvm -o /dev/null %s
// C0.5: also require C → object at -O0/-O2 for the full scalar/SIMD surface
// (public closure gate companion to capi-public-closure-probe.c).
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -emit-obj -O0 -o %t.o0.o %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -emit-obj -O2 -o %t.o2.o %s
// REQUIRES: haydn-registered-target
//
// D208 coverage gate: every wired scalar/SIMD __builtin_haydn_* must lower
// through Clang CodeGen. Regenerated from BuiltinsHaydn.td after golden-lane
// ExtVector retype (X2 → v2i / <2 x i32>, X4 → v4s / <4 x i16>).
//
// EXCLUDED: frexp _pair (pair-frexp-cg.c + x2cmula-isqrt-probe.c for composed
//           x2cmula/x2cmuls frexp pairs), AE (ae-*.c; HaydnAeBuiltin
//           PublicEnabled=0 — not on haydn.h), memory/CB/SFR.
// C0.3: isqrt SoftISqrt is covered below; complex-MAC compose pairs in probe.
// C0.5: emit-obj RUN lines above + capi-public-closure-probe.c for public API.

typedef int __attribute__((ext_vector_type(2))) v2i;
typedef short __attribute__((ext_vector_type(4))) v4s;
volatile long long sink_ll;
volatile int sink_i;
volatile unsigned sink_u;
volatile v2i sink_v2;
volatile v4s sink_v4;

int test_abs32s(int a0) {
  int r = __builtin_haydn_abs32s(a0);
  sink_i = (int)r;
}

long long test_abs64(long long a0) {
  long long r = __builtin_haydn_abs64(a0);
  sink_ll = (long long)r;
}

long long test_abs64s(long long a0) {
  long long r = __builtin_haydn_abs64s(a0);
  sink_ll = (long long)r;
}

int test_add32s(int a0, int a1) {
  int r = __builtin_haydn_add32s(a0, a1);
  sink_i = (int)r;
}

long long test_add64_h(long long a0, long long a1) {
  long long r = __builtin_haydn_add64_h(a0, a1);
  sink_ll = (long long)r;
}

long long test_add64_l(long long a0, long long a1) {
  long long r = __builtin_haydn_add64_l(a0, a1);
  sink_ll = (long long)r;
}

long long test_add64s(long long a0, long long a1) {
  long long r = __builtin_haydn_add64s(a0, a1);
  sink_ll = (long long)r;
}

int test_addbrba32(int a0, int a1) {
  int r = __builtin_haydn_addbrba32(a0, a1);
  sink_i = (int)r;
}

long long test_and64(long long a0, long long a1) {
  long long r = __builtin_haydn_and64(a0, a1);
  sink_ll = (long long)r;
}

// C0.4/C0.5: ImmArg uimm4 — second arg must be ICE (variable form is Sema-error).
int test_arctan(long long a0) {
  int r = __builtin_haydn_arctan(a0, 2);
  sink_i = (int)r;
}

int test_brev32(int a0, int a1) {
  int r = __builtin_haydn_brev32(a0, a1);
  sink_i = (int)r;
}

int test_exp2(int a0) {
  int r = __builtin_haydn_exp2(a0);
  sink_i = (int)r;
}

// C0.3: SoftISqrt pure-ALU path (no native ISQRT / llvm.haydn.isqrt).
int test_isqrt(int a0) {
  int r = __builtin_haydn_isqrt(a0);
  sink_i = (int)r;
}

long long test_f2mulaa32r_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulaa32r_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulaa32r_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulaa32r_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulaa32rs_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulaa32rs_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulaa32rs_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulaa32rs_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulas32r_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulas32r_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulas32r_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulas32r_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulas32rs_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulas32rs_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulas32rs_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulas32rs_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulsa32r_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulsa32r_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulsa32r_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulsa32r_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulsa32rs_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulsa32rs_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulsa32rs_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulsa32rs_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulss32r_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulss32r_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulss32r_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulss32r_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulss32rs_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulss32rs_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulss32rs_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_f2mulss32rs_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_f2mulzaa32r_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzaa32r_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzaa32r_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzaa32r_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzaa32rs_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzaa32rs_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzaa32rs_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzaa32rs_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzas32r_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzas32r_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzas32r_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzas32r_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzas32rs_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzas32rs_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzas32rs_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzas32rs_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzsa32r_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzsa32r_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzsa32r_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzsa32r_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzsa32rs_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzsa32rs_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzsa32rs_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzsa32rs_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzss32r_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzss32r_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzss32r_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzss32r_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzss32rs_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzss32rs_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_f2mulzss32rs_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_f2mulzss32rs_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32r_hh(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32r_hh(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32r_lh(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32r_lh(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32r_ll(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32r_ll(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32rs_hh(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32rs_hh(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32rs_lh(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32rs_lh(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mul32rs_ll(long long a0, long long a1) {
  long long r = __builtin_haydn_ff2mul32rs_ll(a0, a1);
  sink_ll = (long long)r;
}

long long test_ff2mula32r_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32r_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2mula32r_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32r_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2mula32r_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32r_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2mula32rs_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32rs_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2mula32rs_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32rs_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2mula32rs_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2mula32rs_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32r_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32r_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32r_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32r_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32r_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32r_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32rs_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32rs_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32rs_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32rs_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_ff2muls32rs_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_ff2muls32rs_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmul16_hs00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs01(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs01(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs03(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs03(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs11(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs11(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs12(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs12(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs13(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs13(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs23(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs23(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_hs33(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_hs33(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls01(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls01(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls03(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls03(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls11(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls11(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls12(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls12(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls13(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls13(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls23(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls23(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul16_ls33(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul16_ls33(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul32s_hh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul32s_hh(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul32s_lh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul32s_lh(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmul32s_ll(long long a0, long long a1) {
  long long r = __builtin_haydn_fmul32s_ll(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmula16_hs00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs01(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs01(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs03(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs03(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs11(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs11(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs12(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs12(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs13(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs13(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs23(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs23(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_hs33(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_hs33(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls01(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls01(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls03(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls03(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls11(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls11(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls12(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls12(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls13(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls13(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls23(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls23(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula16_ls33(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula16_ls33(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula32s_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula32s_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula32s_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula32s_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmula32s_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmula32s_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_hs_11_00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_hs_11_00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_hs_13_02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_hs_13_02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_hs_33_22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_hs_33_22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_ls_11_00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_ls_11_00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_ls_13_02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_ls_13_02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa16_ls_33_22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa16_ls_33_22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa32s_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa32s_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulaa32s_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulaa32s_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulas32s_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulas32s_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulas32s_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulas32s_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs01(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs01(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs03(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs03(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs11(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs11(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs12(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs12(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs13(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs13(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs23(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs23(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_hs33(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_hs33(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls01(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls01(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls03(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls03(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls11(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls11(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls12(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls12(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls13(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls13(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls23(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls23(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls16_ls33(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls16_ls33(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls32s_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls32s_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls32s_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls32s_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmuls32s_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmuls32s_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulsa32s_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulsa32s_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulsa32s_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulsa32s_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_hs_11_00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_hs_11_00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_hs_13_02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_hs_13_02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_hs_33_22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_hs_33_22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_ls_11_00(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_ls_11_00(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_ls_13_02(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_ls_13_02(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss16_ls_33_22(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss16_ls_33_22(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss32s_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss32s_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulss32s_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_fmulss32s_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_hs_11_00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_hs_11_00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_hs_13_02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_hs_13_02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_hs_33_22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_hs_33_22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_ls_11_00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_ls_11_00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_ls_13_02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_ls_13_02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa16_ls_33_22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa16_ls_33_22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa32s_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa32s_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzaa32s_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzaa32s_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzas32s_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzas32s_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzas32s_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzas32s_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzsa32s_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzsa32s_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzsa32s_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzsa32s_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_hs_11_00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_hs_11_00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_hs_13_02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_hs_13_02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_hs_33_22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_hs_33_22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_ls_11_00(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_ls_11_00(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_ls_13_02(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_ls_13_02(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss16_ls_33_22(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss16_ls_33_22(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss32s_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss32s_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_fmulzss32s_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_fmulzss32s_hllh(a0, a1);
  sink_ll = (long long)r;
}

int test_log2(int a0) {
  int r = __builtin_haydn_log2(a0);
  sink_i = (int)r;
}

int test_mac32(int a0, int a1, int a2) {
  int r = __builtin_haydn_mac32(a0, a1, a2);
  sink_i = (int)r;
}

int test_macq31(int a0, int a1, int a2) {
  int r = __builtin_haydn_macq31(a0, a1, a2);
  sink_i = (int)r;
}

long long test_max64(long long a0, long long a1) {
  long long r = __builtin_haydn_max64(a0, a1);
  sink_ll = (long long)r;
}

v2i test_maxabs32s(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_maxabs32s(a0, a1);
  sink_v2 = r;
}

long long test_min64(long long a0, long long a1) {
  long long r = __builtin_haydn_min64(a0, a1);
  sink_ll = (long long)r;
}

int test_movad32_high(long long a0) {
  int r = __builtin_haydn_movad32_high(a0);
  sink_i = (int)r;
}

int test_movad32_low(long long a0) {
  int r = __builtin_haydn_movad32_low(a0);
  sink_i = (int)r;
}

long long test_movda16(int a0) {
  long long r = __builtin_haydn_movda16(a0);
  sink_ll = (long long)r;
}

long long test_movda32(int a0) {
  long long r = __builtin_haydn_movda32(a0);
  sink_ll = (long long)r;
}

long long test_movda32x2(int a0, int a1) {
  long long r = __builtin_haydn_movda32x2(a0, a1);
  sink_ll = (long long)r;
}

unsigned test_move32_dr_h(long long a0) {
  unsigned r = __builtin_haydn_move32_dr_h(a0);
  sink_u = (unsigned)r;
}

unsigned test_move32_dr_l(long long a0) {
  unsigned r = __builtin_haydn_move32_dr_l(a0);
  sink_u = (unsigned)r;
}

unsigned test_movesfr2gpr(void) {
  unsigned r = __builtin_haydn_movesfr2gpr();
  sink_u = (unsigned)r;
}

long long test_movf64(long long a0) {
  long long r = __builtin_haydn_movf64(a0);
  sink_ll = (long long)r;
}

long long test_movt64(long long a0) {
  long long r = __builtin_haydn_movt64(a0);
  sink_ll = (long long)r;
}

long long test_mul16aq(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mul16aq(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mul16zaq(long long a0, long long a1) {
  long long r = __builtin_haydn_mul16zaq(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_hh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_hh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_hl(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_hl(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_huh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_huh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_hul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_hul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_lh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_lh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ll(int a0, int a1) {
  long long r = __builtin_haydn_mul64_ll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_luh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_luh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_lul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_lul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ss_hh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ss_hh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ss_hl(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ss_hl(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ss_lh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ss_lh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ss_ll(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ss_ll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_su_lul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_su_lul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_su_uhh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_su_uhh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_su_uhl(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_su_uhl(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_su_ulh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_su_ulh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uhh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uhh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uhl(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uhl(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uhuh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uhuh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uhul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uhul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ulh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ulh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ull(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ull(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uluh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uluh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_ulul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_ulul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_us_hul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_us_hul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_us_luh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_us_luh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_us_uhuh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_us_uhuh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_us_uhul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_us_uhul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uu_ulh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uu_ulh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uu_ull(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uu_ull(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uu_uluh(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uu_uluh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mul64_uu_ulul(long long a0, long long a1) {
  long long r = __builtin_haydn_mul64_uu_ulul(a0, a1);
  sink_ll = (long long)r;
}

long long test_mula64_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_huh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_huh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ss_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ss_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ss_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ss_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ss_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ss_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ss_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ss_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_su_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_su_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_su_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_su_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_su_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_su_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_su_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_su_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_us_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_us_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_us_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_us_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_us_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_us_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_us_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_us_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uu_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uu_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uu_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uu_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uu_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uu_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mula64_uu_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mula64_uu_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulaa32_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulaa32_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulaa32_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulaa32_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulaa32s_fir_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulaa32s_fir_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulaa32s_fir_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulaa32s_fir_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulafq16x2_fir_1(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulafq16x2_fir_1(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulafq16x2_fir_3(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulafq16x2_fir_3(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas32_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas32_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas32_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas32_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_huh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_huh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ss_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ss_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ss_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ss_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ss_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ss_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ss_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ss_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_su_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_su_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_su_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_su_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_su_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_su_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_su_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_su_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_us_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_us_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_us_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_us_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_us_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_us_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_us_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_us_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uu_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uu_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uu_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uu_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uu_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uu_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulas64_uu_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulas64_uu_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfc32x16ras_high(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfc32x16ras_high(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfc32x16ras_low(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfc32x16ras_low(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfp32x16x2ras_high(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfp32x16x2ras_high(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfp32x16x2ras_low(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfp32x16x2ras_low(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfq16x2_fir_1(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfq16x2_fir_1(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulfq16x2_fir_3(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulfq16x2_fir_3(a0, a1, a2);
  sink_ll = (long long)r;
}

int test_mull(int a0, int a1) {
  int r = __builtin_haydn_mull(a0, a1);
  sink_i = (int)r;
}

int test_mulq31(int a0, int a1, int a2) {
  int r = __builtin_haydn_mulq31(a0, a1, a2);
  sink_i = (int)r;
}

long long test_mulq63(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulq63(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_huh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_huh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ss_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ss_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ss_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ss_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ss_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ss_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ss_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ss_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_su_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_su_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_su_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_su_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_su_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_su_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_su_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_su_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_us_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_us_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_us_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_us_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_us_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_us_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_us_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_us_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uu_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uu_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uu_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uu_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uu_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uu_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_muls64_uu_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_muls64_uu_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulsa32_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulsa32_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulsa32_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulsa32_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss32_hhll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss32_hhll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss32_hllh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss32_hllh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_huh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_huh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ss_hh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ss_hh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ss_hl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ss_hl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ss_lh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ss_lh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ss_ll(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ss_ll(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_su_lul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_su_lul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_su_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_su_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_su_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_su_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_su_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_su_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uhh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uhh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uhl(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uhl(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_us_hul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_us_hul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_us_luh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_us_luh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_us_uhuh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_us_uhuh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_us_uhul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_us_uhul(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uu_ulh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uu_ulh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uu_ull(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uu_ull(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uu_uluh(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uu_uluh(a0, a1, a2);
  sink_ll = (long long)r;
}

long long test_mulss64_uu_ulul(long long a0, long long a1, long long a2) {
  long long r = __builtin_haydn_mulss64_uu_ulul(a0, a1, a2);
  sink_ll = (long long)r;
}

int test_mulssh(int a0, int a1) {
  int r = __builtin_haydn_mulssh(a0, a1);
  sink_i = (int)r;
}

int test_mulsuh(int a0, int a1) {
  int r = __builtin_haydn_mulsuh(a0, a1);
  sink_i = (int)r;
}

unsigned test_muluuh(unsigned a0, unsigned a1) {
  unsigned r = __builtin_haydn_muluuh(a0, a1);
  sink_u = (unsigned)r;
}

long long test_mulzaa32_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzaa32_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzaa32_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzaa32_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzas32_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzas32_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzas32_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzas32_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzsa32_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzsa32_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzsa32_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzsa32_hllh(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzss32_hhll(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzss32_hhll(a0, a1);
  sink_ll = (long long)r;
}

long long test_mulzss32_hllh(long long a0, long long a1) {
  long long r = __builtin_haydn_mulzss32_hllh(a0, a1);
  sink_ll = (long long)r;
}

int test_neg32s(int a0) {
  int r = __builtin_haydn_neg32s(a0);
  sink_i = (int)r;
}

long long test_neg64(long long a0) {
  long long r = __builtin_haydn_neg64(a0);
  sink_ll = (long long)r;
}

long long test_neg64s(long long a0) {
  long long r = __builtin_haydn_neg64s(a0);
  sink_ll = (long long)r;
}

long long test_not64(long long a0) {
  long long r = __builtin_haydn_not64(a0);
  sink_ll = (long long)r;
}

int test_nsa16_l(int a0) {
  int r = __builtin_haydn_nsa16_l(a0);
  sink_i = (int)r;
}

int test_nsa32(int a0) {
  int r = __builtin_haydn_nsa32(a0);
  sink_i = (int)r;
}

int test_nsa32_l(int a0) {
  int r = __builtin_haydn_nsa32_l(a0);
  sink_i = (int)r;
}

int test_nsa64(int a0) {
  int r = __builtin_haydn_nsa64(a0);
  sink_i = (int)r;
}

int test_nsau32(int a0) {
  int r = __builtin_haydn_nsau32(a0);
  sink_i = (int)r;
}

int test_nsaz16_l(int a0) {
  int r = __builtin_haydn_nsaz16_l(a0);
  sink_i = (int)r;
}

int test_nsaz32_l(int a0) {
  int r = __builtin_haydn_nsaz32_l(a0);
  sink_i = (int)r;
}

int test_nsaz64(int a0) {
  int r = __builtin_haydn_nsaz64(a0);
  sink_i = (int)r;
}

long long test_or64(long long a0, long long a1) {
  long long r = __builtin_haydn_or64(a0, a1);
  sink_ll = (long long)r;
}

/* packsr32/satsr64/packsr32x2_* removed: not golden encodings (haydn_dsp.h). */
long long test_sra64r(long long a0, int a1) {
  long long r = __builtin_haydn_sra64r(a0, a1);
  sink_ll = (long long)r;
}

int test_popcount32(int a0) {
  int r = __builtin_haydn_popcount32(a0);
  sink_i = (int)r;
}

int test_popcount64(long long a0) {
  int r = __builtin_haydn_popcount64(a0);
  sink_i = (int)r;
}

int test_recip(int a0) {
  int r = __builtin_haydn_recip(a0);
  sink_i = (int)r;
}

long long test_seq64(long long a0) {
  long long r = __builtin_haydn_seq64(a0);
  sink_ll = (long long)r;
}

long long test_sle64(long long a0) {
  long long r = __builtin_haydn_sle64(a0);
  sink_ll = (long long)r;
}

long long test_sll64(long long a0, int a1) {
  long long r = __builtin_haydn_sll64(a0, a1);
  sink_ll = (long long)r;
}

long long test_slt64(long long a0) {
  long long r = __builtin_haydn_slt64(a0);
  sink_ll = (long long)r;
}

long long test_smula16_00(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_00(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_10(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_10(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_11(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_11(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_20(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_20(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_21(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_21(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_22(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_22(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_30(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_30(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_31(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_31(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_32(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_32(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16_33(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16_33(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_00(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_00(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_10(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_10(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_11(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_11(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_20(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_20(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_21(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_21(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_22(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_22(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_30(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_30(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_31(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_31(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_32(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_32(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smula16s_33(long long a0, long long a1) {
  long long r = __builtin_haydn_smula16s_33(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_00(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_00(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_10(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_10(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_11(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_11(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_20(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_20(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_21(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_21(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_22(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_22(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_30(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_30(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_31(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_31(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_32(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_32(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16_33(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16_33(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_00(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_00(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_10(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_10(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_11(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_11(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_20(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_20(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_21(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_21(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_22(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_22(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_30(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_30(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_31(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_31(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_32(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_32(a0, a0, a1);
  sink_ll = (long long)r;
}

long long test_smuls16s_33(long long a0, long long a1) {
  long long r = __builtin_haydn_smuls16s_33(a0, a0, a1);
  sink_ll = (long long)r;
}

int test_sqrt(int a0) {
  int r = __builtin_haydn_sqrt(a0);
  sink_i = (int)r;
}

long long test_sra64(long long a0, int a1) {
  long long r = __builtin_haydn_sra64(a0, a1);
  sink_ll = (long long)r;
}

/* test_sra64r defined once above (golden i64,i32 -> i64). */

long long test_srai64r(long long a0, int a1) {
  long long r = __builtin_haydn_srai64r(a0, 1);
  sink_ll = (long long)r;
}

long long test_srl64(long long a0, int a1) {
  long long r = __builtin_haydn_srl64(a0, a1);
  sink_ll = (long long)r;
}

int test_sub32s(int a0, int a1) {
  int r = __builtin_haydn_sub32s(a0, a1);
  sink_i = (int)r;
}

long long test_sub64_h(long long a0, long long a1) {
  long long r = __builtin_haydn_sub64_h(a0, a1);
  sink_ll = (long long)r;
}

long long test_sub64_l(long long a0, long long a1) {
  long long r = __builtin_haydn_sub64_l(a0, a1);
  sink_ll = (long long)r;
}

long long test_sub64s(long long a0, long long a1) {
  long long r = __builtin_haydn_sub64s(a0, a1);
  sink_ll = (long long)r;
}

v2i test_x2abs32(v2i a0) {
  v2i r = __builtin_haydn_x2abs32(a0);
  sink_v2 = r;
}

v2i test_x2abs32s(v2i a0) {
  v2i r = __builtin_haydn_x2abs32s(a0);
  sink_v2 = r;
}

v2i test_x2add32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2add32(a0, a1);
  sink_v2 = r;
}

v2i test_x2add32_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2add32_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2add32s(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2add32s(a0, a1);
  sink_v2 = r;
}

v2i test_x2add32s_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2add32s_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2addsub32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2addsub32(a0, a1);
  sink_v2 = r;
}

v2i test_x2addsub32_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2addsub32_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2addsub32s(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2addsub32s(a0, a1);
  sink_v2 = r;
}

v2i test_x2addsub32s_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2addsub32s_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2clamp32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2clamp32(a0, a1, a0);
  sink_v2 = r;
}

long long test_x2dot32(v2i a0, v2i a1) {
  long long r = __builtin_haydn_x2dot32(a0, a1);
  sink_ll = (long long)r;
}

v2i test_x2fcmul32rs(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fcmul32rs(a0, a1);
  sink_v2 = r;
}

v2i test_x2fcmul32rss(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fcmul32rss(a0, a1);
  sink_v2 = r;
}

v2i test_x2fcmula32rs(v2i a0, v2i a1, v2i a2) {
  v2i r = __builtin_haydn_x2fcmula32rs(a0, a1, a2);
  sink_v2 = r;
}

v2i test_x2fcmula32rss(v2i a0, v2i a1, v2i a2) {
  v2i r = __builtin_haydn_x2fcmula32rss(a0, a1, a2);
  sink_v2 = r;
}

v2i test_x2ff2rsst32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2ff2rsst32(a0, a1);
  sink_v2 = r;
}

v2i test_x2ff2rst32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2ff2rst32(a0, a1);
  sink_v2 = r;
}

v2i test_x2fmul32rs(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmul32rs(a0, a1);
  sink_v2 = r;
}

v2i test_x2fmul32rss(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmul32rss(a0, a1);
  sink_v2 = r;
}

v2i test_x2fmul32ts(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmul32ts(a0, a1);
  sink_v2 = r;
}

v2i test_x2fmula32rs(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmula32rs(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2fmula32rss(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmula32rss(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2fmula32ts(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmula32ts(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2fmuls32rs(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmuls32rs(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2fmuls32rss(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmuls32rss(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2fmuls32ts(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2fmuls32ts(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2frsst32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2frsst32(a0, a1);
  sink_v2 = r;
}

v2i test_x2frst32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2frst32(a0, a1);
  sink_v2 = r;
}

long long test_x2hadd32_h(v2i a0) {
  long long r = __builtin_haydn_x2hadd32_h(a0);
  sink_ll = (long long)r;
}

long long test_x2hadd32_l(v2i a0) {
  long long r = __builtin_haydn_x2hadd32_l(a0);
  sink_ll = (long long)r;
}

long long test_x2hadd32s_h(v2i a0) {
  long long r = __builtin_haydn_x2hadd32s_h(a0);
  sink_ll = (long long)r;
}

long long test_x2hadd32s_l(v2i a0) {
  long long r = __builtin_haydn_x2hadd32s_l(a0);
  sink_ll = (long long)r;
}

long long test_x2hmax32(v2i a0) {
  long long r = __builtin_haydn_x2hmax32(a0);
  sink_ll = (long long)r;
}

long long test_x2hmin32(v2i a0) {
  long long r = __builtin_haydn_x2hmin32(a0);
  sink_ll = (long long)r;
}

v2i test_x2max32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2max32(a0, a1);
  sink_v2 = r;
}

v2i test_x2min32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2min32(a0, a1);
  sink_v2 = r;
}

v2i test_x2mjswap32(v2i a0) {
  v2i r = __builtin_haydn_x2mjswap32(a0);
  sink_v2 = r;
}

v2i test_x2mjswap32s(v2i a0) {
  v2i r = __builtin_haydn_x2mjswap32s(a0);
  sink_v2 = r;
}

v2i test_x2movf32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2movf32(a0, a1);
  sink_v2 = r;
}

v2i test_x2movt32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2movt32(a0, a1);
  sink_v2 = r;
}

v2i test_x2mulaph32(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulaph32(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2mulapl32(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulapl32(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2mulph32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulph32(a0, a1);
  sink_v2 = r;
}

v2i test_x2mulpl32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulpl32(a0, a1);
  sink_v2 = r;
}

v2i test_x2mulsph32(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulsph32(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2mulspl32(v2i acc, v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2mulspl32(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x2neg32(v2i a0) {
  v2i r = __builtin_haydn_x2neg32(a0);
  sink_v2 = r;
}

v2i test_x2neg32_l(v2i a0) {
  v2i r = __builtin_haydn_x2neg32_l(a0);
  sink_v2 = r;
}

v2i test_x2neg32s(v2i a0) {
  v2i r = __builtin_haydn_x2neg32s(a0);
  sink_v2 = r;
}

v2i test_x2neg32s_l(v2i a0) {
  v2i r = __builtin_haydn_x2neg32s_l(a0);
  sink_v2 = r;
}

v2i test_x2sel32_hh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sel32_hh(a0, a1);
  sink_v2 = r;
}

v2i test_x2sel32_hl(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sel32_hl(a0, a1);
  sink_v2 = r;
}

v2i test_x2sel32_lh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sel32_lh(a0, a1);
  sink_v2 = r;
}

v2i test_x2sel32_ll(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sel32_ll(a0, a1);
  sink_v2 = r;
}

v2i test_x2seq32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2seq32(a0, a1);
  sink_v2 = r;
}

v2i test_x2sle32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sle32(a0, a1);
  sink_v2 = r;
}

v2i test_x2sll32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2sll32(a0, a1);
  sink_v2 = r;
}

v2i test_x2slli32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2slli32(a0, 1);
  sink_v2 = r;
}

v2i test_x2slt32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2slt32(a0, a1);
  sink_v2 = r;
}

v2i test_x2sra32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2sra32(a0, a1);
  sink_v2 = r;
}

v2i test_x2sra32r(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2sra32r(a0, a1);
  sink_v2 = r;
}

v2i test_x2srai32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2srai32(a0, 1);
  sink_v2 = r;
}

v2i test_x2srai32r(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2srai32r(a0, 1);
  sink_v2 = r;
}

v2i test_x2srl32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2srl32(a0, a1);
  sink_v2 = r;
}

v2i test_x2srli32(v2i a0, int a1) {
  v2i r = __builtin_haydn_x2srli32(a0, 1);
  sink_v2 = r;
}

v2i test_x2sub32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sub32(a0, a1);
  sink_v2 = r;
}

v2i test_x2sub32_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sub32_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2sub32s(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sub32s(a0, a1);
  sink_v2 = r;
}

v2i test_x2sub32s_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2sub32s_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2subadd32(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2subadd32(a0, a1);
  sink_v2 = r;
}

v2i test_x2subadd32_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2subadd32_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2subadd32s(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2subadd32s(a0, a1);
  sink_v2 = r;
}

v2i test_x2subadd32s_hllh(v2i a0, v2i a1) {
  v2i r = __builtin_haydn_x2subadd32s_hllh(a0, a1);
  sink_v2 = r;
}

v2i test_x2swap32(v2i a0) {
  v2i r = __builtin_haydn_x2swap32(a0);
  sink_v2 = r;
}

v4s test_x4abs16(v4s a0) {
  v4s r = __builtin_haydn_x4abs16(a0);
  sink_v4 = r;
}

v4s test_x4abs16s(v4s a0) {
  v4s r = __builtin_haydn_x4abs16s(a0);
  sink_v4 = r;
}

v4s test_x4add16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4add16(a0, a1);
  sink_v4 = r;
}

v4s test_x4add16s(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4add16s(a0, a1);
  sink_v4 = r;
}

v2i test_x4cjmul16s_h(v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cjmul16s_h(a0, a1);
  sink_v2 = r;
}

v2i test_x4cjmul16s_l(v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cjmul16s_l(a0, a1);
  sink_v2 = r;
}

v2i test_x4cjmula16s_h(v2i acc, v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cjmula16s_h(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x4cjmula16s_l(v2i acc, v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cjmula16s_l(acc, a0, a1);
  sink_v2 = r;
}

v4s test_x4clamp16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4clamp16(a0, a1, a0);
  sink_v4 = r;
}

v2i test_x4cmul16(v4s a0) {
  v2i r = __builtin_haydn_x4cmul16(a0);
  sink_v2 = r;
}

v2i test_x4cmul16_f2(v4s a0) {
  v2i r = __builtin_haydn_x4cmul16_f2(a0);
  sink_v2 = r;
}

v2i test_x4cmul16s(v4s a0) {
  v2i r = __builtin_haydn_x4cmul16s(a0);
  sink_v2 = r;
}

v2i test_x4cmul16s_f2(v4s a0) {
  v2i r = __builtin_haydn_x4cmul16s_f2(a0);
  sink_v2 = r;
}

v2i test_x4cmul16s_h(v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cmul16s_h(a0, a1);
  sink_v2 = r;
}

v2i test_x4cmul16s_l(v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cmul16s_l(a0, a1);
  sink_v2 = r;
}

v2i test_x4cmula16s_h(v2i acc, v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cmula16s_h(acc, a0, a1);
  sink_v2 = r;
}

v2i test_x4cmula16s_l(v2i acc, v4s a0, v4s a1) {
  v2i r = __builtin_haydn_x4cmula16s_l(acc, a0, a1);
  sink_v2 = r;
}

v4s test_x4conj16(v4s a0) {
  v4s r = __builtin_haydn_x4conj16(a0);
  sink_v4 = r;
}

v4s test_x4conj16s(v4s a0) {
  v4s r = __builtin_haydn_x4conj16s(a0);
  sink_v4 = r;
}

long long test_x4dot16(v4s a0, v4s a1) {
  long long r = __builtin_haydn_x4dot16(a0, a1);
  sink_ll = (long long)r;
}

long long test_x4energy16(v4s a0) {
  long long r = __builtin_haydn_x4energy16(a0);
  sink_ll = (long long)r;
}

v4s test_x4fcmul16rs(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4fcmul16rs(a0, a1);
  sink_v4 = r;
}

v4s test_x4fcmul16rss(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4fcmul16rss(a0, a1);
  sink_v4 = r;
}

v4s test_x4fcmula16rs(v4s a0, v4s a1, v4s a2) {
  v4s r = __builtin_haydn_x4fcmula16rs(a0, a1, a2);
  sink_v4 = r;
}

v4s test_x4fcmula16rss(v4s a0, v4s a1, v4s a2) {
  v4s r = __builtin_haydn_x4fcmula16rss(a0, a1, a2);
  sink_v4 = r;
}

v4s test_x4fmul16rs(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4fmul16rs(a0, a1);
  sink_v4 = r;
}

v4s test_x4fmul16rss(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4fmul16rss(a0, a1);
  sink_v4 = r;
}

v4s test_x4fmul16ts(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4fmul16ts(a0, a1);
  sink_v4 = r;
}

v4s test_x4frsst16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4frsst16(a0, a1);
  sink_v4 = r;
}

v4s test_x4frst16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4frst16(a0, a1);
  sink_v4 = r;
}

long long test_x4hadd16_h(v4s a0) {
  long long r = __builtin_haydn_x4hadd16_h(a0);
  sink_ll = (long long)r;
}

long long test_x4hadd16_l(v4s a0) {
  long long r = __builtin_haydn_x4hadd16_l(a0);
  sink_ll = (long long)r;
}

long long test_x4hmax16(v4s a0) {
  long long r = __builtin_haydn_x4hmax16(a0);
  sink_ll = (long long)r;
}

long long test_x4hmin16(v4s a0) {
  long long r = __builtin_haydn_x4hmin16(a0);
  sink_ll = (long long)r;
}

v4s test_x4max16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4max16(a0, a1);
  sink_v4 = r;
}

v4s test_x4min16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4min16(a0, a1);
  sink_v4 = r;
}

v4s test_x4mjswap16(v4s a0) {
  v4s r = __builtin_haydn_x4mjswap16(a0);
  sink_v4 = r;
}

v4s test_x4mjswap16s(v4s a0) {
  v4s r = __builtin_haydn_x4mjswap16s(a0);
  sink_v4 = r;
}

v4s test_x4movf16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4movf16(a0, a1);
  sink_v4 = r;
}

v4s test_x4movt16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4movt16(a0, a1);
  sink_v4 = r;
}

v4s test_x4neg16(v4s a0) {
  v4s r = __builtin_haydn_x4neg16(a0);
  sink_v4 = r;
}

v4s test_x4neg16s(v4s a0) {
  v4s r = __builtin_haydn_x4neg16s(a0);
  sink_v4 = r;
}

v4s test_x4sat32t16(v2i a0, v2i a1) {
  v4s r = __builtin_haydn_x4sat32t16(a0, a1);
  sink_v4 = r;
}

v4s test_x4seli16(v4s a0, v4s a1, int a2) {
  v4s r = __builtin_haydn_x4seli16(a0, a1, 1);
  sink_v4 = r;
}

v4s test_x4seq16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4seq16(a0, a1);
  sink_v4 = r;
}

v4s test_x4sle16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4sle16(a0, a1);
  sink_v4 = r;
}

v4s test_x4sll16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4sll16(a0, a1);
  sink_v4 = r;
}

v4s test_x4slli16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4slli16(a0, 1);
  sink_v4 = r;
}

v4s test_x4slt16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4slt16(a0, a1);
  sink_v4 = r;
}

v4s test_x4sra16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4sra16(a0, a1);
  sink_v4 = r;
}

v4s test_x4sra16r(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4sra16r(a0, a1);
  sink_v4 = r;
}

v4s test_x4srai16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4srai16(a0, 1);
  sink_v4 = r;
}

v4s test_x4srai16r(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4srai16r(a0, 1);
  sink_v4 = r;
}

v4s test_x4srl16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4srl16(a0, a1);
  sink_v4 = r;
}

v4s test_x4srli16(v4s a0, int a1) {
  v4s r = __builtin_haydn_x4srli16(a0, 1);
  sink_v4 = r;
}

v4s test_x4sub16(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4sub16(a0, a1);
  sink_v4 = r;
}

v4s test_x4sub16s(v4s a0, v4s a1) {
  v4s r = __builtin_haydn_x4sub16s(a0, a1);
  sink_v4 = r;
}

v4s test_x4swap16(v4s a0) {
  v4s r = __builtin_haydn_x4swap16(a0);
  sink_v4 = r;
}
