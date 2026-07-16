//===-- m6-scl-divide24x24.c - scl_divide24x24 port -----------------------===//
//
// NatureDSP port: scl_divide24x24 -> Haydn. In NatureDSP this is a trivial
// wrapper around scl_divide32x32 with the operands shifted left by 8 bits
// (scl_divide24x24_hifi3.c:94-97):
//   return scl_divide32x32(x>>8, y>>8);
//
// Porting strategy: identical wrapper around our Haydn port of
// scl_divide32x32. No AE_DIV64D32_H use; lowers to __divsi3.
//
// Return format (matches NatureDSP): bits 23:0 fractional, bits 31:24 exp.
//
// Pipeline: clang -target haydn-unknown-elf -S -> llvm-mc -> ld.lld -> ELF.
//===----------------------------------------------------------------------===//

#include "haydn_dsp.h"

/// Inlined scl_divide32x32 from m6-scl-divide32x32.c to keep this kernel
/// self-contained (no cross-object dependency).
static inline __attribute__((__always_inline__))
uint32_t scl_divide32x32_local(int32_t x, int32_t y) {
  int expx = __haydn_nsaz32_l(x);
  int expy = __haydn_nsaz32_l(y);
  int32_t nx = x << expx;
  int32_t ny = y << expy;
  int64_t ratio = ((int64_t)nx << 31) / (int64_t)ny;  /* -> __divsi3 / __divdi3 */
  int32_t frac = (int32_t)(ratio >> 8);
  uint32_t exp = (uint32_t)(expy - expx + 1) & 0xFF;
  return (exp << 24) | ((uint32_t)frac & 0x00FFFFFFu);
}

/// 24-bit fractional scalar divide. Wrapper around scl_divide32x32.
uint32_t scl_divide24x24(int32_t x, int32_t y) {
  return scl_divide32x32_local(x >> 8, y >> 8);
}

int32_t gx = 0x400000;  /* 0.5 in Q23 */
int32_t gy = 0x600000;  /* 0.75 in Q23 */
uint32_t gres;

int main(void) {
  gres = scl_divide24x24(gx, gy);
  return (int)gres;
}
