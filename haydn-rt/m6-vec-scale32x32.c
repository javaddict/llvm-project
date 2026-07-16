//===-- m6-vec-scale32x32.c - vector scalar multiply port -----------------===//
//
// NatureDSP port: vec_scale32x32 -> Haydn. Elementwise scalar multiply of
// an i32 vector by a Q31 fractional scale factor using __haydn_fmul32s_hh
// (fractional 32x32 -> 64 multiply, high*high lane) then PACKSR32 to Q31.
// This exercises the FMUL32S fractional multiply path and PACKSR32 output
// pack (without accumulator-MAC dependence).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 0x10000000, 0x20000000, 0x30000000, 0x40000000,
             0x50000000, 0x60000000, 0x70000000, 0x7FFFFFFF };
int z[N];

/* Fractional gain (Q31). Output Q31 = SAT((Q31_in * Q31_gain + rounding) >> 31). */
void vec_scale32x32(int *restrict zout, const int *restrict xin, int gain_q31) {
    for (int i = 0; i < N; ++i) {
        long long prod = __haydn_fmul32s_hh((long long)xin[i],
                                            (long long)gain_q31);
        zout[i] = __haydn_packsr32(prod, 31);
    }
}

int main(void) {
    /* Q31 = 0.5 -> 0x40000000. */
    vec_scale32x32(z, x, 0x40000000);
    return z[0];
}
