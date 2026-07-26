// RUN: %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -verify %s
// A.4: baremetal feature gates — TLS and wide _BitInt fail closed.

// expected-error@+1 {{thread-local storage is not supported for the current target}}
__thread int tls_var;

// expected-error@+1 {{signed _BitInt of bit sizes greater than 64 not supported}}
_BitInt(128) wide_bitint(void);

// GPR constraint OK
void ok_r(void) {
  int x;
  __asm__ volatile("" : "=r"(x));
}

// DR constraint rejected until backend implements 'd'
void bad_d(void) {
  long long x;
  // expected-error@+1 {{invalid output constraint '=d' in asm}}
  __asm__ volatile("" : "=d"(x));
}
