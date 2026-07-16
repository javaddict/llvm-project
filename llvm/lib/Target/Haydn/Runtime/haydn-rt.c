// Haydn baremetal runtime stubs
// Provides integer arithmetic helper functions required by the Haydn backend.
// The backend emits libcalls for operations without hardware support
// (division, 64-bit multiply).
// The helpers here are the INTEGER-ONLY libcalls (__divsi3/__udivsi3
// __modsi3/__umodsi3/__muldi3/__divdi3/__udivdi3/__moddi3/__umoddi3). They are
// non-recursive (pure integer arithmetic) and are emitted by the backend for
// targets without a matching hardware instruction; they are slow but correct.
// SOFT-FLOAT AND FLOATING-POINT CONVERSION HELPERS ARE INTENTIONALLY ABSENT
// from this file. Per hard constraint #4 (soft-float by default) those helpers
// come from compiler-rt (builtins) or the standalone haydn-rt/softfloat.c
// (Berkeley SoftFloat-derived; patch 0009 fixed its __fixsfsi truncation)
// NOT from a naive C reimplementation here. A previous version of this file
// contained stubs like `__addsf3 { return a + b; }` that lowered to the very
// libcall they implemented, giving infinite recursion / stack overflow on
// first use; they have been removed. An undefined symbol at link time
// is strictly better than silent infinite recursion: the linker fails loudly
// whereas the recursion crashes at runtime with no diagnostic.

//===----------------------------------------------------------------------===//
// Integer arithmetic (32-bit)
//===----------------------------------------------------------------------===//

// Signed 32-bit division
int __divsi3(int a, int b) {
  // Simple restoring division, handles sign correctly
  int neg = 0;
  if (a < 0) { a = -a; neg = !neg; }
  if (b < 0) { b = -b; neg = !neg; }
  int result = 0;
  while (a >= b) { a -= b; result++; }
  return neg ? -result : result;
}

// Unsigned 32-bit division
unsigned int __udivsi3(unsigned int a, unsigned int b) {
  unsigned int result = 0;
  while (a >= b) { a -= b; result++; }
  return result;
}

// Signed 32-bit remainder
int __modsi3(int a, int b) {
  int neg = 0;
  if (a < 0) { a = -a; neg = 1; }
  if (b < 0) { b = -b; }
  while (a >= b) { a -= b; }
  return neg ? -a : a;
}

// Unsigned 32-bit remainder
unsigned int __umodsi3(unsigned int a, unsigned int b) {
  while (a >= b) { a -= b; }
  return a;
}

//===----------------------------------------------------------------------===//
// Integer arithmetic (64-bit)
//===----------------------------------------------------------------------===//

// 64-bit multiply (returns low 32 bits of result)
// The Haydn backend uses __muldi3 for i64 multiply.
typedef unsigned int u32;
typedef int i32;

// 64-bit signed multiply
long long __muldi3(long long a, long long b) {
  // Decompose into 16-bit parts to avoid needing 64-bit hardware multiply
  u32 al = (u32)(a & 0xFFFF);
  u32 ah = (u32)((a >> 16) & 0xFFFF);
  u32 bl = (u32)(b & 0xFFFF);
  u32 bh = (u32)((b >> 16) & 0xFFFF);

  long long result = (long long)al * bl;
  result += ((long long)al * bh) << 16;
  result += ((long long)ah * bl) << 16;
  return result;
}

// 64-bit signed division
long long __divdi3(long long a, long long b) {
  int neg = 0;
  if (a < 0) { a = -a; neg = !neg; }
  if (b < 0) { b = -b; neg = !neg; }

  // Simple bit-at-a-time long division
  long long result = 0;
  int shift = 0;
  long long divisor = b;
  while (divisor <= a && divisor > 0 && shift < 64) {
    divisor <<= 1;
    shift++;
  }
  while (shift > 0) {
    shift--;
    divisor >>= 1;
    if (a >= divisor) {
      a -= divisor;
      result |= (1LL << shift);
    }
  }
  return neg ? -result : result;
}

// 64-bit unsigned division
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
  unsigned long long result = 0;
  int shift = 0;
  unsigned long long divisor = b;
  while (divisor <= a && shift < 64) {
    divisor <<= 1;
    shift++;
  }
  while (shift > 0) {
    shift--;
    divisor >>= 1;
    if (a >= divisor) {
      a -= divisor;
      result |= (1ULL << shift);
    }
  }
  return result;
}

// 64-bit signed remainder
long long __moddi3(long long a, long long b) {
  int neg = 0;
  if (a < 0) { a = -a; neg = 1; }
  if (b < 0) { b = -b; }

  int shift = 0;
  long long divisor = b;
  while (divisor <= a && divisor > 0 && shift < 64) {
    divisor <<= 1;
    shift++;
  }
  while (shift > 0) {
    shift--;
    divisor >>= 1;
    if (a >= divisor) {
      a -= divisor;
    }
  }
  return neg ? -a : a;
}

// 64-bit unsigned remainder
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
  int shift = 0;
  unsigned long long divisor = b;
  while (divisor <= a && shift < 64) {
    divisor <<= 1;
    shift++;
  }
  while (shift > 0) {
    shift--;
    divisor >>= 1;
    if (a >= divisor) {
      a -= divisor;
    }
  }
  return a;
}

//===----------------------------------------------------------------------===//
// Soft-float / FP conversion helpers — DELIBERATELY ABSENT (see header)
//===----------------------------------------------------------------------===//
//
// __addsf3, __mulsf3, __subsf3, __divsf3, __adddf3, __muldf3, __fixsfsi
// __floatsisf, __lesf2 were removed. They were recursive stubs that
// lowered to the very libcall they implemented. Provide these from
// compiler-rt (builtins) or the standalone haydn-rt/softfloat.c instead.
