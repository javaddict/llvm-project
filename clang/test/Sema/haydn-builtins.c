// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -verify %s
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

int64_t test_cb_pair_null(int base, int cbr, int stride) {
  // expected-warning@+1 {{null passed to a callee that requires a non-null argument}}
  return __builtin_haydn_ldw_cb_imm_pair((int *)0, base, cbr, stride);
}

int64_t test_cb_pair_ok(int base, int cbr, int stride) {
  int np;
  return __builtin_haydn_ldw_cb_imm_pair(&np, base, cbr, stride);
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

int test_packsr32_ok(int64_t a) {
  return __builtin_haydn_packsr32(a, 0);
}
int test_packsr32_bad(int64_t a) {
  // expected-error@+1 {{argument value 64 is outside the valid range [0, 63]}}
  return __builtin_haydn_packsr32(a, 64);
}

// *i* forms require a constant imm (ISA encoding); use variable-shift ops for regs.
