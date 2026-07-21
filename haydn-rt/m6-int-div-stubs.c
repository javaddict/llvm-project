//===-- m6-int-div-stubs.c - integer division libcall stubs ---------------===//
//
// Minimal standalone implementations of the 8 integer-division libcalls the
// Haydn backend emits for G_SDIV/UDIV/SREM/UREM. Provided so the M6
// division kernels link end-to-end without pulling in the full
// haydn-rt.c (which transitively requires soft-float helpers __ltsf2 etc.
// that are unrelated to the division path).
//
// For production use, link against compiler-rt instead (per haydn-rt.c:7-8).
//
// Algorithms are bit-at-a-time shift-subtract; correct but O(n) per bit.
//===----------------------------------------------------------------------===//

typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;

/// Signed 32-bit division
int __divsi3(int a, int b) {
  int neg = 0;
  unsigned int ua, ub;
  if (a < 0) { ua = (unsigned int)(-a); neg = !neg; } else ua = (unsigned int)a;
  if (b < 0) { ub = (unsigned int)(-b); neg = !neg; } else ub = (unsigned int)b;
  unsigned int q = 0, r = ua;
  if (ub != 0) {
    unsigned int bit = 1;
    while ((u64)ub < ((u64)1u << 31) && (u64)ub * 2 <= ua) { ub <<= 1; bit <<= 1; }
    while (bit) { if (r >= ub) { r -= ub; q |= bit; } ub >>= 1; bit >>= 1; }
  }
  return neg ? -(int)q : (int)q;
}

/// Unsigned 32-bit division
unsigned int __udivsi3(unsigned int a, unsigned int b) {
  if (b == 0) return 0;
  unsigned int q = 0, r = a, bit = 1;
  while (b < (1u << 31) && (u64)b * 2 <= a) { b <<= 1; bit <<= 1; }
  while (bit) { if (r >= b) { r -= b; q |= bit; } b >>= 1; bit >>= 1; }
  return q;
}

/// Signed 32-bit remainder
int __modsi3(int a, int b) {
  int neg = (a < 0);
  unsigned int ua = neg ? (unsigned int)(-a) : (unsigned int)a;
  unsigned int ub = (b < 0) ? (unsigned int)(-b) : (unsigned int)b;
  unsigned int r = ua;
  if (ub != 0) {
    while (r >= ub) r -= ub;
  }
  return neg ? -(int)r : (int)r;
}

/// Unsigned 32-bit remainder
unsigned int __umodsi3(unsigned int a, unsigned int b) {
  if (b == 0) return a;
  while (a >= b) a -= b;
  return a;
}

/// Signed 64-bit division
long long __divdi3(long long a, long long b) {
  int neg = 0;
  u64 ua, ub;
  if (a < 0) { ua = (u64)(-a); neg = !neg; } else ua = (u64)a;
  if (b < 0) { ub = (u64)(-b); neg = !neg; } else ub = (u64)b;
  u64 q = 0, r = ua;
  if (ub != 0) {
    unsigned bit = 0;
    u64 d = ub;
    while ((d & (1ULL << 63)) == 0 && d * 2 <= ua) { d <<= 1; bit++; }
    while (1) {
      if (r >= d) { r -= d; q |= (1ULL << bit); }
      if (bit == 0) break;
      d >>= 1; bit--;
    }
  }
  return neg ? -(long long)q : (long long)q;
}

/// Unsigned 64-bit division
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
  if (b == 0) return 0;
  u64 q = 0, r = a;
  unsigned bit = 0;
  u64 d = b;
  while ((d & (1ULL << 63)) == 0 && d * 2 <= a) { d <<= 1; bit++; }
  while (1) {
    if (r >= d) { r -= d; q |= (1ULL << bit); }
    if (bit == 0) break;
    d >>= 1; bit--;
  }
  return q;
}

/// Signed 64-bit remainder
long long __moddi3(long long a, long long b) {
  int neg = (a < 0);
  u64 ua = neg ? (u64)(-a) : (u64)a;
  u64 ub = (b < 0) ? (u64)(-b) : (u64)b;
  u64 r = ua;
  if (ub != 0) {
    u64 d = ub;
    while ((d & (1ULL << 63)) == 0 && d * 2 <= ua) d <<= 1;
    while (d >= ub) {
      if (r >= d) r -= d;
      d >>= 1;
    }
  }
  return neg ? -(long long)r : (long long)r;
}

/// Unsigned 64-bit remainder
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
  if (b == 0) return a;
  u64 d = b;
  while ((d & (1ULL << 63)) == 0 && d * 2 <= a) d <<= 1;
  while (d >= b) {
    if (a >= d) a -= d;
    d >>= 1;
  }
  return a;
}
