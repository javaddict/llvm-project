//===-- m6-vec-hadd32.c - horizontal add reduce port ----------------------===//
//
// NatureDSP port: vector L2-style sum reduce using X2HADD32_L (dual 32-bit
// horizontal add) to fold two lanes per DR64 into a scalar accumulator. The
// NatureDSP analog is vec_dot32x32 with one vector all-ones. Exercises the
// X2HADD32_L horizontal-reduction instruction.
//
// Validates the X2HADD32_L horizontal-add code path in a reduction loop.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8   /* 4 DR64 lanes = 8 i32 */

int x[N] = { 1, 2, 3, 4, 5, 6, 7, 8 };

int vec_hadd32(const int *restrict xin, int n) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    int acc = 0;
    for (int i = 0; i < n / 2; ++i) {
        /* X2HADD32_L returns DR64 with both lanes set to low+high sum. */
        long long h = __haydn_x2hadd32_l(px[i]);
        acc += (int)(h & 0xFFFFFFFFLL);
    }
    return acc;
}

int main(void) {
    int s = vec_hadd32(x, N);
    return s;
}
