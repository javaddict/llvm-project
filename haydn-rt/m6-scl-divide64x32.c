//===-- m6-scl-divide64x32.c - scl_divide64x32 port -----------------------===//
//
// NatureDSP port: scl_divide64x32 -> Haydn. 64-bit signed integer divided by
// 32-bit signed integer, returning the saturated 32-bit quotient. This is the
// kernel that most directly exercises AE_DIV64D32_H (32 uses in NatureDSP).
//
// Porting strategy (per ~/haydn-plans/m6-division-scope.md §4): the NatureDSP
// source invokes AE_DIV64D32_H 32 times in a row because the HiFi instruction
// emits one bit of quotient per cycle. Haydn has no hardware divide (ISA-16
// gap), so the AE_DIV64D32_H shim in haydn_dsp.h lowers each call to a single
// C `/` -> __divdi3 libcall. The semantic is preserved: produce x/y.
//
// Overflow handling matches the NatureDSP source: if |y| > |x_high| the
// quotient fits in 32 bits; otherwise saturate to 0x7FFFFFFF or 0x80000000
// based on input signs (scl_divide64x32_hifi3.c:155-198).
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

/// Saturated 64/32 -> 32 signed integer divide.
/// Returns INT32_MAX / INT32_MIN on overflow (sign of inputs picked).
int32_t scl_divide64x32(int64_t x, int32_t y) {
  /* Sign bookkeeping */
  int xs = (x < 0);
  int ys = (y < 0);
  /* Saturate sign: quotient is negative iff signs differ. */
  int neg_q = xs ^ ys;

  /* AE_DIV64D32_H computes |x|/|y| truncated to high-32; on Haydn we use the
   * full 64-bit divide (the AE_DIV64D32_H shim) which lowers to __divdi3.
   * The redundant repeats in NatureDSP are idempotent here. */
  int64_t ax = x < 0 ? -x : x;
  int64_t ay = y < 0 ? -(int64_t)y : y;

  /* Overflow detection: if divisor's high 32 bits are 0 (typical) the result
   * fits. The NatureDSP version checks |y| > truncate_high32(|x|) for the
   * no-overflow case. We use the equivalent test on the full values. */
  int overflows = (ay == 0) || (ax / ay > 0x7FFFFFFFLL);

  int32_t q;
  if (overflows) {
    q = neg_q ? (int32_t)0x80000000 : (int32_t)0x7FFFFFFF;
  } else {
    /* Native __divdi3 path via AE_DIV64D32_H. */
    int64_t full = AE_DIV64D32_H((ae_int64)x, (ae_int32)y);
    q = (int32_t)full;
    (void)ax; (void)ay; (void)xs; (void)ys;
  }
  return q;
}

/* Test harness */
int64_t numer = 0x123456789ABCDEF0LL;
int32_t denom = 0x1234;
int32_t result;

int main(void) {
  result = scl_divide64x32(numer, denom);
  return (int)result;
}
