// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -verify %s
//
// C0.4 ConstArg/ImmRange contract: ImmCheck on encoding fields rejects
// non-ICE and out-of-range constants. Public switch-literal wrappers are
// covered in Headers/haydn-ar-unaligned-intrinsics.c.

typedef long long int64_t;
typedef int haydn_x2int32 __attribute__((__vector_size__(8)));
typedef short haydn_x4int16 __attribute__((__vector_size__(8)));

// --- Scalar imm shifts (ImmArg parity) ------------------------------------
int test_slli32_ok(int a) { return __builtin_haydn_slli32(a, 5); }
int test_slli32_nonconst(int a, int s) {
  // expected-error@+1 {{argument to '__builtin_haydn_slli32' must be a constant integer}}
  return __builtin_haydn_slli32(a, s);
}
int64_t test_srai64_ok(int64_t a) { return __builtin_haydn_srai64(a, 3); }
int64_t test_srai64_bad(int64_t a) {
  // expected-error@+1 {{argument value 64 is outside the valid range [0, 63]}}
  return __builtin_haydn_srai64(a, 64);
}

// --- Vector imm shifts / lane select --------------------------------------
haydn_x2int32 test_x2slli_ok(haydn_x2int32 a) {
  return __builtin_haydn_x2slli32(a, 1);
}
haydn_x2int32 test_x2slli_nonconst(haydn_x2int32 a, int s) {
  // expected-error@+1 {{argument to '__builtin_haydn_x2slli32' must be a constant integer}}
  return __builtin_haydn_x2slli32(a, s);
}
haydn_x4int16 test_x4seli_ok(haydn_x4int16 a, haydn_x4int16 b) {
  return __builtin_haydn_x4seli16(a, b, 7);
}
haydn_x4int16 test_x4seli_bad(haydn_x4int16 a, haydn_x4int16 b) {
  // expected-error@+1 {{argument value 16 is outside the valid range [0, 15]}}
  return __builtin_haydn_x4seli16(a, b, 16);
}

// --- UA AR ImmArg ---------------------------------------------------------
void test_wbar_ok(void *p) { __builtin_haydn_wbarwua(1, p, 0); }
void test_wbar_ar_bad(void *p) {
  // expected-error@+1 {{argument value 4 is outside the valid range [0, 3]}}
  __builtin_haydn_wbarwua(4, p, 0);
}
void test_wbar_dir_nonconst(void *p, int d) {
  // expected-error@+1 {{argument to '__builtin_haydn_wbarwua' must be a constant integer}}
  __builtin_haydn_wbarwua(0, p, d);
}
void test_sqhwua_ok(int64_t d, void *p, int s) {
  __builtin_haydn_d_sqhwua_post(d, p, 2, s, 1);
}
void test_sqhwua_ar_nonconst(int64_t d, void *p, int s, int ar) {
  // expected-error@+1 {{argument to '__builtin_haydn_d_sqhwua_post' must be a constant integer}}
  __builtin_haydn_d_sqhwua_post(d, p, ar, s, 0);
}

// --- CB REG cbr_sel ImmArg ------------------------------------------------
int64_t test_ldw_cb_reg_ok(const void *base, int stride) {
  void *np;
  return __builtin_haydn_ldw_cb_reg_pair(&np, base, 1, stride);
}
int64_t test_ldw_cb_reg_nonconst(const void *base, int cbr, int stride) {
  void *np;
  // expected-error@+1 {{argument to '__builtin_haydn_ldw_cb_reg_pair' must be a constant integer}}
  return __builtin_haydn_ldw_cb_reg_pair(&np, base, cbr, stride);
}

// --- sin_cos / movei / arctan ---------------------------------------------
int64_t test_sin_cos_nonconst(int phase, int n) {
  // expected-error@+1 {{argument to '__builtin_haydn_sin_cos' must be a constant integer}}
  return __builtin_haydn_sin_cos(phase, n);
}
int64_t test_movei_l_ok(void) { return __builtin_haydn_movei_l(-1); }
int64_t test_movei_l_nonconst(int v) {
  // expected-error@+1 {{argument to '__builtin_haydn_movei_l' must be a constant integer}}
  return __builtin_haydn_movei_l(v);
}
