//===-- m6-vec-add.c - vec_add32x32_fast port -----------------------------===//
//
// NatureDSP port: vec_add32x32_fast -> Haydn. Pairwise saturated 32-bit add
// of two vectors using the X2ADD32S DR64 SIMD instruction (2 lanes per reg).
// Algorithm is a faithful port; the NatureDSP AE_ADD32S/LD32X2/ST32X2
// memory + saturating-add inner loop maps directly onto X2ADD32S.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 1, 2, 3, 4, 5, 6, 7, 8 };
int y[N] = { 10, 20, 30, 40, 50, 60, 70, 80 };
int z[N];

void vec_add32x32_fast(int *restrict zout,
                       const int *restrict xin,
                       const int *restrict yin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    const haydn_dr64_t *restrict py = (const haydn_dr64_t *)yin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;

    for (int i = 0; i < N / 2; ++i) {
        haydn_dr64_t a = px[i];
        haydn_dr64_t b = py[i];
        haydn_dr64_t r = __haydn_x2add32s(a, b);
        pz[i] = r;
    }
}

int main(void) {
    vec_add32x32_fast(z, x, y);
    return z[0];
}
