//===-- m6-vec-divide16x16.c - vec_divide16x16 port -----------------------===//
//
// NatureDSP port: vec_divide16x16 -> Haydn. Pairwise Q15 fractional divide.
// NatureDSP uses Newton-Raphson reciprocal-multiply (3 iterations of
// AE_MULFP16X4S_vector); we use plain C `/` -> __divsi3.
//
// Porting strategy: normalize via Haydn NSA16, compute fractional Q15 divide
// via C `/`, write separate frac[] and exp[] arrays.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

#define N 8

/// Vector Q15 fractional divide. Writes frac[N] (Q(15-exp)) and exp[N].
void vec_divide16x16(int16_t *restrict frac, int16_t *restrict exp,
                     const int16_t *restrict x,
                     const int16_t *restrict y, int M) {
  for (int n = 0; n < M; ++n) {
    int16_t xn = x[n];
    int16_t yn = y[n];
    int expx = __haydn_nsaz16_l(xn);
    int expy = __haydn_nsaz16_l(yn);
    int32_t nx = ((int32_t)xn) << expx;
    int32_t ny = ((int32_t)yn) << expy;
    /* Fractional Q15 divide: (nx << 15) / ny -> __divsi3 */
    int32_t ratio = (nx << 15) / ny;
    frac[n] = (int16_t)(ratio & 0xFFFF);
    exp[n] = (int16_t)(expy - expx + 1);
  }
}

int16_t gx[N] = { 0x4000, 0x3000, 0x2000, 0x1000, 0x6000, 0x5000, 0x7000, 0x2000 };
int16_t gy[N] = { 0x6000, 0x5000, 0x4000, 0x2000, 0x7000, 0x6000, 0x7800, 0x4000 };
int16_t gfrac[N];
int16_t gexp[N];

int main(void) {
  vec_divide16x16(gfrac, gexp, gx, gy, N);
  return (int)gfrac[0];
}
