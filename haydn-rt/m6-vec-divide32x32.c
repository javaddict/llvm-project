//===-- m6-vec-divide32x32.c - vec_divide32x32 port -----------------------===//
//
// NatureDSP port: vec_divide32x32 -> Haydn. Pairwise Q31 fractional divide of
// two vectors, writing fractional part (Q(31-exp)) and a separate exponent
// vector. NatureDSP uses Newton-Raphson reciprocal-multiply (4 iterations
// of AE_MULSFP32X2RAS + AE_MULAFP32X2RAS); those 32-bit fractional MAC
// intrinsics in haydn_dsp.h are arity-broken so we use plain C `/`.
//
// Porting strategy: normalize via Haydn NSA32, compute fractional divide
// via C `/` (-> __divsi3), pack (frac, exp) pair outputs.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

#define N 4

/// Vector Q31 fractional divide. Writes frac[N] (Q(31-exp)) and exp[N].
void vec_divide32x32(int32_t *restrict frac, int16_t *restrict exp,
                     const int32_t *restrict x,
                     const int32_t *restrict y, int M) {
  for (int n = 0; n < M; ++n) {
    int32_t xn = x[n];
    int32_t yn = y[n];
    /* Normalize via Haydn native NSA. */
    int expx = __haydn_nsaz32_l(xn);
    int expy = __haydn_nsaz32_l(yn);
    int32_t nx = xn << expx;
    int32_t ny = yn << expy;
    /* Fractional Q31 divide: (nx << 31) / ny; the low 31 bits are the Q31
     * fraction. -> __divsi3 */
    int64_t ratio = ((int64_t)nx << 31) / (int64_t)ny;
    frac[n] = (int32_t)(ratio >> 8);  /* right-justify Q23 in low bits */
    exp[n] = (int16_t)(expy - expx + 1);
  }
}

int32_t gx[N] = { 0x40000000, 0x30000000, 0x20000000, 0x10000000 };
int32_t gy[N] = { 0x60000000, 0x50000000, 0x40000000, 0x20000000 };
int32_t gfrac[N];
int16_t gexp[N];

int main(void) {
  vec_divide32x32(gfrac, gexp, gx, gy, N);
  return (int)gfrac[0];
}
