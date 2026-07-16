//===-- m6-vec-divide64x32i.c - vec_divide64x32i port ---------------------===//
//
// NatureDSP port: vec_divide64x32i -> Haydn. Vector 64-bit signed integer /
// 32-bit signed integer -> 32-bit saturated quotient per element. The
// NatureDSP source uses AE_DIV64D32_H (for even-index) and AE_DIV64D32_L
// (for odd-index) 32 times each per element pair (one bit per cycle).
//
// Porting strategy: AE_DIV64D32_H and AE_DIV64D32_L shims in haydn_dsp.h
// both lower to a single C `/` -> __divdi3. The 32 redundant repeats are
// idempotent. Saturation: if |y| > high32(|x|) the result fits; otherwise
// the kernel saturates to 0x7fffffff or 0x80000000 by input sign.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

#define N 4

/// Vector 64/32 -> 32 integer divide, per-element saturated.
void vec_divide64x32i(int32_t *restrict frac,
                      const int64_t *restrict x,
                      const int32_t *restrict y, int M) {
  for (int n = 0; n < M; ++n) {
    int64_t xn = x[n];
    int32_t yn = y[n];
    int xs = (xn < 0);
    int ys = (yn < 0);
    int neg_q = xs ^ ys;
    int64_t ax = xn < 0 ? -xn : xn;
    int64_t ay = yn < 0 ? -(int64_t)yn : yn;
    int overflows = (ay == 0) || (ax / ay > 0x7FFFFFFFLL);

    int32_t q;
    if (overflows) {
      q = neg_q ? (int32_t)0x80000000 : (int32_t)0x7FFFFFFF;
    } else {
      /* AE_DIV64D32_H path -> __divdi3. The 32 redundant NatureDSP repeats
       * are dropped (idempotent on Haydn). */
      ae_int64 q64 = AE_DIV64D32_H((ae_int64)xn, (ae_int32)yn);
      q = (int32_t)q64;
    }
    frac[n] = q;
  }
}

int64_t x[N] = { 0x100000000LL, 0x200000001LL, -0x300000000LL, 0x7FFFFFFFLL };
int32_t y[N] = { 0x10, 0x20, -0x30, 0x40 };
int32_t frac[N];

int main(void) {
  vec_divide64x32i(frac, x, y, N);
  return (int)frac[0];
}
