// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -verify %s
//
// Sema checks from BuiltinsHaydn.td → haydn_builtin_sema.inc:
//   - frexp-pattern _pair: null out-pointer → warn_null_arg
//   - shift / select / pack immediates: constant range checks
// Golden lanes: X2 pair/shift take <2 x i32>, X4 take <4 x i16>.

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));

//===----------------------------------------------------------------------===//
// Frexp-pattern pair: out-pointer should not be a null constant
//===----------------------------------------------------------------------===//

int64_t test_pair_ok(haydn_x2int32 a, haydn_x2int32 b) {
  int64_t lo;
  return __builtin_haydn_x2mul32_pair(&lo, a, b);
}

int64_t test_pair_null(haydn_x2int32 a, haydn_x2int32 b) {
  // expected-warning@+1 {{null passed to a callee that requires a non-null argument}}
  return __builtin_haydn_x2mul32_pair((int64_t *)0, a, b);
}

int64_t test_cb_pair_null(const void *base) {
  // expected-warning@+1 {{null passed to a callee that requires a non-null argument}}
  return __builtin_haydn_ldw_cb_imm_pair((void **)0, base, /*cbr=*/0, /*stride=*/1);
}

int64_t test_cb_pair_ok(const void *base) {
  void *np;
  return __builtin_haydn_ldw_cb_imm_pair(&np, base, /*cbr=*/0, /*stride=*/1);
}

// LS IMM offset / CB ImmArg ranges
long long test_d_ldw_with_imm_ok(const void *base) {
  return __builtin_haydn_d_ldw_with_imm(base, 0);
}
long long test_d_ldw_with_imm_bad(const void *base) {
  // expected-error@+1 {{argument value 32 is outside the valid range [-32, 31]}}
  return __builtin_haydn_d_ldw_with_imm(base, 32);
}
void test_d_sdw_with_imm_bad(long long data, void *base) {
  // expected-error@+1 {{argument value -33 is outside the valid range [-32, 31]}}
  __builtin_haydn_d_sdw_with_imm(data, base, -33);
}
int64_t test_ldw_cb_imm_cbr_bad(const void *base) {
  void *np;
  // expected-error@+1 {{argument value 2 is outside the valid range [0, 1]}}
  return __builtin_haydn_ldw_cb_imm_pair(&np, base, 2, 1);
}

//===----------------------------------------------------------------------===//
// Immediate ranges (last arg) — error when constant is out of range
//===----------------------------------------------------------------------===//

haydn_x2int32 test_x2slli32_ok(haydn_x2int32 a) {
  return __builtin_haydn_x2slli32(a, 0);
}
haydn_x2int32 test_x2slli32_hi(haydn_x2int32 a) {
  return __builtin_haydn_x2slli32(a, 31);
}
haydn_x2int32 test_x2slli32_bad(haydn_x2int32 a) {
  // expected-error@+1 {{argument value 32 is outside the valid range [0, 31]}}
  return __builtin_haydn_x2slli32(a, 32);
}

haydn_x4int16 test_x4slli16_ok(haydn_x4int16 a) {
  return __builtin_haydn_x4slli16(a, 15);
}
haydn_x4int16 test_x4slli16_bad(haydn_x4int16 a) {
  // expected-error@+1 {{argument value 16 is outside the valid range [0, 15]}}
  return __builtin_haydn_x4slli16(a, 16);
}

haydn_x4int16 test_x4seli16_ok(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4seli16(a, b, 0);
}
haydn_x4int16 test_x4seli16_bad(haydn_x4int16 a, haydn_x4int16 b) {
  // expected-error@+1 {{argument value 16 is outside the valid range [0, 15]}}
  return __builtin_haydn_x4seli16(a, b, 16);
}

// packsr32 is not a builtin (composite in haydn_dsp.h). ImmArg range on SRAI64R:
int64_t test_srai64r_ok(int64_t a) {
  return __builtin_haydn_srai64r(a, 0);
}
int64_t test_srai64r_bad(int64_t a) {
  // expected-error@+1 {{argument value 64 is outside the valid range [0, 63]}}
  return __builtin_haydn_srai64r(a, 64);
}

// *i* forms require a constant imm (ISA encoding); use variable-shift ops for regs.

// SETCBR cbr_sel is ICE uimm1 [0,1] (CSR index); product headers only pass 0/1.
void test_setcbr_begin_ok(int base) {
  __builtin_haydn_setcbr_begin(0, base);
  __builtin_haydn_setcbr_begin(1, base);
}
void test_setcbr_begin_bad(int base) {
  // expected-error@+1 {{argument value 2 is outside the valid range [0, 1]}}
  __builtin_haydn_setcbr_begin(2, base);
}
void test_setcbr_end_bad(int base) {
  // expected-error@+1 {{argument value -1 is outside the valid range [0, 1]}}
  __builtin_haydn_setcbr_end(-1, base);
}

// SIN_COS uimm4 [0,15] — ImmCheck already on builtin; range smoke test.
int64_t test_sin_cos_ok(int phase) {
  return __builtin_haydn_sin_cos(phase, 0);
}
int64_t test_sin_cos_bad(int phase) {
  // expected-error@+1 {{argument value 16 is outside the valid range [0, 15]}}
  return __builtin_haydn_sin_cos(phase, 16);
}

// C0.4 / G-SEMA-INTRIN: UA ar_sel/dir are ImmArg encoding fields.
// Constant ar_sel legal; runtime ar_sel fails Sema (switch wrappers in
// haydn_dsp.h / haydn.h specials for NatureDSP ar&=3).
// CB-149 closed: the full 2-bit ar_sel domain (AR0..AR3) is restored —
// golden field width 2 bits, BundleSim executes int64_t ar[4], NatureDSP
// documents ar&=3.
void test_flar_const_ok(void) {
  __builtin_haydn_flar(0);
  __builtin_haydn_flar(3);
}
void test_flar_runtime_bad(int ar) {
  // expected-error@+1 {{argument to '__builtin_haydn_flar' must be a constant integer}}
  __builtin_haydn_flar(ar);
}
void test_flar_range_bad(void) {
  // expected-error@+1 {{argument value 4 is outside the valid range [0, 3]}}
  __builtin_haydn_flar(4);
}
void test_pldwwua_const_ok(const void *p) {
  __builtin_haydn_pldwwua(0, p);
  __builtin_haydn_pldwwua(2, p);
}
void test_pldwwua_runtime_bad(int ar, const void *p) {
  // expected-error@+1 {{argument to '__builtin_haydn_pldwwua' must be a constant integer}}
  __builtin_haydn_pldwwua(ar, p);
}
long long test_ltwua_dir_bad(const void *p, int stride) {
  // expected-error@+1 {{argument value 2 is outside the valid range [0, 1]}}
  return __builtin_haydn_d_ltwua_post(p, 0, stride, 2);
}
void test_setcbr_runtime_bad(int sel, int base) {
  // expected-error@+1 {{argument to '__builtin_haydn_setcbr_begin' must be a constant integer}}
  __builtin_haydn_setcbr_begin(sel, base);
}
int test_arctan_ok(long long xy) {
  return __builtin_haydn_arctan(xy, 0);
}
int test_arctan_bad(long long xy) {
  // expected-error@+1 {{argument value 16 is outside the valid range [0, 15]}}
  return __builtin_haydn_arctan(xy, 16);
}
long long test_movei_h_ok(void) {
  return __builtin_haydn_movei_h(0x12345678);
}
long long test_movei_h_runtime_bad(int v) {
  // expected-error@+1 {{argument to '__builtin_haydn_movei_h' must be a constant integer}}
  return __builtin_haydn_movei_h(v);
}
