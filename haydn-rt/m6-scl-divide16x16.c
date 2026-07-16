//===-- m6-scl-divide16x16.c - scl_divide16x16 port -----------------------===//
//
// NatureDSP port: scl_divide16x16 -> Haydn. Returns packed (exp:16 | frac:16)
// for Q15 fractional division. The NatureDSP source uses Newton-Raphson
// reciprocal-multiply (3 iterations of AE_MULFP16X4S_vector); Haydn's
// 16-bit fractional MAC intrinsics are arity-broken so we use plain C `/`.
//
// Porting strategy: lower via C `/` (-> __divsi3). Normalize via NSA, do the
// fractional divide in Q15, pack result.
//
// Return format: bits 15:0 fractional, bits 31:16 exponent.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

/// Q15 fractional scalar divide: returns packed (exp:16 | frac:16).
uint32_t scl_divide16x16(int16_t x, int16_t y) {
  /* Normalize using Haydn NSA. The 16-bit form uses NSAZ16_0. */
  int expx = __haydn_nsaz16_l(x);
  int expy = __haydn_nsaz16_l(y);

  int32_t nx = ((int32_t)x) << expx;
  int32_t ny = ((int32_t)y) << expy;

  /* Q15 fractional divide: (nx << 15) / ny, lower 16 bits is the fraction.
   * -> __divsi3 */
  int32_t ratio = (nx << 15) / ny;
  int16_t frac = (int16_t)(ratio & 0xFFFF);

  uint32_t exp = (uint32_t)(expy - expx + 1) & 0xFFFF;
  return (exp << 16) | ((uint32_t)(uint16_t)frac);
}

int16_t gx = 0x4000;  /* 0.5 in Q15 */
int16_t gy = 0x6000;  /* 0.75 in Q15 */
uint32_t gres;

int main(void) {
  gres = scl_divide16x16(gx, gy);
  return (int)gres;
}
