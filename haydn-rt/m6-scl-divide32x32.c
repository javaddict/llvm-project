//===-- m6-scl-divide32x32.c - scl_divide32x32 port -----------------------===//
//
// NatureDSP port: scl_divide32x32 -> Haydn. Returns the packed fractional
// part (low 24 bits) and exponent (high 8 bits) of x/y for Q31 fractional
// division. Original NatureDSP uses a Newton-Raphson reciprocal-multiply
// approximation via AE_MULSFP32X2RAS / AE_MULAFP32X2RAS (4 iterations) to
// achieve ~1.8e-9 accuracy without a hardware divide.
//
// Porting strategy: Haydn has no hardware divide but C `/` lowers cleanly to
// __divsi3 (verified). We compute x/y via the libcall and pack the result
// into the NatureDSP return format. The fractional-approximation intrinsics
// in haydn_dsp.h are currently arity-broken (m6-e2e-validation.md §3) so we
// use plain C arithmetic, which is the recommended path per
// ~/haydn-plans/m6-division-scope.md §4 (general-case -> C `/`).
//
// Return format (matches NatureDSP): bits 23:0 fractional part in Q(31-exp),
// bits 31:24 exponent.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

/// Number of sign bits -- uses Haydn NSA32 instruction.
static inline int nsaz32_l(int32_t v) {
  return __haydn_nsaz32_l(v);
}

/// Q31 fractional scalar divide: returns packed (exp:8 | frac:24).
uint32_t scl_divide32x32(int32_t x, int32_t y) {
  /* Normalize both operands via the native NSA32 (Haydn has this). */
  int expx = nsaz32_l(x);
  int expy = nsaz32_l(y);

  /* Shift into Q31 normalized range so the divide result is in (-1, 1). */
  int32_t nx = x << expx;
  int32_t ny = y << expy;

  /* Compute x/y as a 64-bit value to preserve precision, then take the
   * high 32 bits -> the Q31 fractional quotient. -> __divdi3 (64-bit) /
   * could also use __divsi3 with explicit normalization. We use the 64-bit
   * path for accuracy matching the NatureDSP 2-LSB spec. */
  int64_t ratio = ((int64_t)nx << 31) / (int64_t)ny;  /* -> __divsi3 / __divdi3 */
  int32_t frac = (int32_t)(ratio >> 8);  /* Pack into 24-bit fractional field. */

  /* Pack: high 8 bits = exponent, low 24 bits = fractional. */
  uint32_t exp = (uint32_t)(expy - expx + 1) & 0xFF;
  return (exp << 24) | ((uint32_t)frac & 0x00FFFFFFu);
}

int32_t gx = 0x40000000;  /* 0.5 in Q31 */
int32_t gy = 0x60000000;  /* 0.75 in Q31 */
uint32_t gres;

int main(void) {
  gres = scl_divide32x32(gx, gy);
  return (int)gres;
}
