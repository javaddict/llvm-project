//===-- m6-vec-sub.c - vec_sub32x32 port ----------------------------------===//
//
// NatureDSP port: vec_sub32x32_fast -> Haydn. Pairwise saturated 32-bit
// subtract of two vectors via the X2SUB32S DR64 SIMD instruction. Mirrors
// vec_add32x32_fast; exercises the X2SUB32S code path.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 100, 200, 300, 400, 500, 600, 700, 800 };
int y[N] = { 10, 20, 30, 40, 50, 60, 70, 80 };
int z[N];

void vec_sub32x32_fast(int *restrict zout,
                       const int *restrict xin,
                       const int *restrict yin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    const haydn_dr64_t *restrict py = (const haydn_dr64_t *)yin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;
    for (int i = 0; i < N / 2; ++i) {
        haydn_dr64_t a = px[i];
        haydn_dr64_t b = py[i];
        pz[i] = __haydn_x2sub32s(a, b);
    }
}

int main(void) {
    vec_sub32x32_fast(z, x, y);
    return z[0];
}
