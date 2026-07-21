//===-- m6-fir-acorr32x32.c - FIR autocorrelation port --------------------===//
//
// NatureDSP port: fir_acorr32x32 -> Haydn. Lag-k autocorrelation:
// r[k] = sum_n x[n] * x[n+k] for k = 0..K-1. Inner loop is a 64-bit
// signed-signed accumulator-MAC (MULA64_SS_LL), exactly the FIR inner-loop
// pattern but with both operands from the same buffer. Validates the D99
// 3-arg accumulator MAC in a different access pattern than the dot product.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NSAMPS 8
#define NLAGS  4

int x[NSAMPS] = { 100, 200, 300, 400, 500, 600, 700, 800 };
long long r[NLAGS];

void fir_acorr32x32(const int *restrict xin, long long *restrict rout,
                    int nsamps, int nlags) {
    for (int k = 0; k < nlags; ++k) {
        long long acc = 0;
        for (int n = 0; n + k < nsamps; ++n) {
            acc = __haydn_mula64_ss_ll(acc, (long long)xin[n],
                                       (long long)xin[n + k]);
        }
        rout[k] = acc;
    }
}

int main(void) {
    fir_acorr32x32(x, r, NSAMPS, NLAGS);
    return (int)r[0];
}
