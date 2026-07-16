//===-- m6-vec-mul32x32.c - SIMD multiply port ----------------------------===//
//
// NatureDSP port: vec_mult32x32 (elementwise product) -> Haydn. Dual 32-bit
// SIMD multiply using X2MUL32 (ternary form: rs1, rs2, ra). The NatureDSP
// source (vec_mult32x32_hifi3.c) uses AE_MUL32S / AE_MUL32P; the Haydn X2MUL32
// packs two i32 multiplies per DR64 register.
//
// Validates the X2MUL32 (dual 32-bit SIMD multiply, ternary) code path.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 4    /* N/2 DR64 lanes */

long long x[4] = { 2, 3, 4, 5 };
long long y[4] = { 10, 20, 30, 40 };
long long z[4];

void vec_mul32x32(long long *restrict zout,
                  const long long *restrict xin,
                  const long long *restrict yin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    const haydn_dr64_t *restrict py = (const haydn_dr64_t *)yin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;
    for (int i = 0; i < N / 2; ++i) {
        /* Destructive-accumulator constraint: result must alias the first
         * operand. Initialize the lane to the product of the inputs by using
         * px[i] as the destination/first operand and adding py[i] in. */
        haydn_dr64_t acc = px[i];
        pz[i] = __haydn_x2mul32(acc, px[i], py[i]);
    }
}

int main(void) {
    vec_mul32x32(z, x, y);
    return (int)z[0];
}
