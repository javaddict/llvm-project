//===-- m6-vec-divide24x24.c - vec_divide24x24 port -----------------------===//
//
// NatureDSP port: vec_divide24x24 -> Haydn. Pairwise Q23 fractional divide.
// NatureDSP uses Newton-Raphson reciprocal-multiply (same kernel as
// vec_divide32x32 with right-shifted operands). We use plain C `/`.
//
// Porting strategy: normalize via Haydn NSA32 (operands zero-extended into
// 32-bit), compute fractional Q23 divide via C `/` -> __divsi3, write
// separate frac[] and exp[] arrays. Operands are sign-extended 24-bit values
// stored as int32_t (low 24 bits valid).
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

#define N 4

/// Vector Q23 fractional divide. Inputs are 24-bit signed values stored in
/// int32_t. Writes frac[N] (Q(23-exp)) and exp[N].
void vec_divide24x24(int32_t *restrict frac, int16_t *restrict exp,
                     const int32_t *restrict x,
                     const int32_t *restrict y, int M) {
  for (int n = 0; n < M; ++n) {
    int32_t xn = x[n] << 8;  /* Q23 -> Q31 */
    int32_t yn = y[n] << 8;
    int expx = __haydn_nsaz32_l(xn);
    int expy = __haydn_nsaz32_l(yn);
    int32_t nx = xn << expx;
    int32_t ny = yn << expy;
    /* Fractional Q31 divide of the Q23-promoted operands -> __divsi3 */
    int64_t ratio = ((int64_t)nx << 31) / (int64_t)ny;
    /* Re-justify to Q23 output: shift result right 8 to land in Q23. */
    frac[n] = (int32_t)(ratio >> 16);  /* Q23 -> output */
    exp[n] = (int16_t)(expy - expx + 1);
  }
}

int32_t gx[N] = { 0x400000, 0x300000, 0x200000, 0x100000 };
int32_t gy[N] = { 0x600000, 0x500000, 0x400000, 0x200000 };
int32_t gfrac[N];
int16_t gexp[N];

int main(void) {
  vec_divide24x24(gfrac, gexp, gx, gy, N);
  return (int)gfrac[0];
}
