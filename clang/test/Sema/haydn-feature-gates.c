// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -verify %s
// A.4: baremetal feature gates — TLS and wide _BitInt fail closed.
// T-ABI7: C __int128 and _Float16 are Sema-reject (not a product CC).
// Raw IR i128 lowering remains T-ABI6 residual; do not treat -fforce-enable-int128
// as documenting a C ABI.

// expected-error@+1 {{thread-local storage is not supported for the current target}}
__thread int tls_var;

// expected-error@+1 {{signed _BitInt of bit sizes greater than 64 not supported}}
_BitInt(128) wide_bitint(void);

// expected-error@+1 {{__int128 is not supported on this target}}
__int128 unsupported_i128;

// expected-error@+1 {{_Float16 is not supported on this target}}
_Float16 unsupported_f16;

// GPR constraint OK on i32. i64 must use 'd' (no silent DR rebind).
void ok_r(void) {
  int x;
  __asm__ volatile("" : "=r"(x));
}

void ok_d(void) {
  long long x;
  __asm__ volatile("" : "=d"(x));
}

void bad_r_i64(void) {
  long long x;
  // expected-error@+1 {{invalid output size for constraint '=r'}}
  __asm__ volatile("" : "=r"(x));
}

void bad_d_i32(void) {
  int x;
  // expected-error@+1 {{invalid output size for constraint '=d'}}
  __asm__ volatile("" : "=d"(x));
}

void bad_q(void) {
  int x;
  // expected-error@+1 {{invalid output constraint '=q' in asm}}
  __asm__ volatile("" : "=q"(x));
}

// Immediate constraints are not a product surface. 'm' is a Clang-generic
// memory letter; backend still has no product mem constraint lowering.
void bad_i(void) {
  int x;
  // expected-error@+1 {{invalid output constraint '=i' in asm}}
  __asm__ volatile("" : "=i"(x));
}
