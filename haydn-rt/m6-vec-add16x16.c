//===-- m6-vec-add16x16.c - vec_add16x16 port (X4 SIMD) -------------------===//
//
// NatureDSP port: vec_add16x16_fast -> Haydn. Pairwise saturated 16-bit add
// of two vectors using the X4ADD16S DR64 SIMD instruction (4 lanes per reg).
// Mirrors vec_add32x32_fast but exercises the 16-bit SIMD datapath instead.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

/* 8 16-bit samples = 2 DR64 lanes (4 x i16 each). */
#define N 8

short x[N] = { 100, 200, 300, 400, 500, 600, 700, 800 };
short y[N] = { 10, 20, 30, 40, 50, 60, 70, 80 };
short z[N];

void vec_add16x16_fast(short *restrict zout,
                       const short *restrict xin,
                       const short *restrict yin) {
    const haydn_dr64_t *restrict px = (const haydn_dr64_t *)xin;
    const haydn_dr64_t *restrict py = (const haydn_dr64_t *)yin;
    haydn_dr64_t *restrict pz = (haydn_dr64_t *)zout;
    /* N=8 short = 16 bytes = 2 DR64 iterations. */
    for (int i = 0; i < N / 4; ++i) {
        haydn_dr64_t a = px[i];
        haydn_dr64_t b = py[i];
        pz[i] = __haydn_x4add16s(a, b);
    }
}

int main(void) {
    vec_add16x16_fast(z, x, y);
    return z[0];
}
