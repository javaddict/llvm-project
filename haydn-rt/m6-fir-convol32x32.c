//===-- m6-fir-convol32x32.c - FIR convolution port -----------------------===//
//
// NatureDSP port: fir_convol32x32 / vec_dot32x32 -> Haydn. Symmetric-tap FIR
// convolution: y[n] = sum_k h[k] * x[n-k]. Inner loop is a multiply-accumulate
// of Q31 samples, the canonical accumulator-MAC pattern. The NatureDSP source
// uses AE_MULA64_SS_LL with a 64-bit accumulator; the Haydn equivalent is
// __haydn_mula64_ss_ll (3-arg accumulator form post-D99). This exercises the
// headline D99 3-arg accumulator MAC fix end-to-end.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NTAPS 4
#define NOUT  8

int h[NTAPS] = { 0x10000000, 0x20000000, 0x40000000, 0x20000000 }; /* Q31-ish */
int x_in[NTAPS + NOUT] = {
    0, 0, 0, 0,
    0x40000000, 0x40000000, 0x40000000, 0x40000000,
    0x40000000, 0x40000000, 0x40000000, 0x40000000
};
int y_out[NOUT];

/* Tap-by-tap Q31 convolution with 64-bit accumulator. Each tap is a signed-
 * signed low-low 64-bit multiply-accumulate (acc += a*b). */
void fir_convol32x32(const int *restrict hcoef, const int *restrict xsamp,
                     int *restrict ysamp, int ntaps, int nout) {
    for (int n = 0; n < nout; ++n) {
        long long acc = 0;
        for (int k = 0; k < ntaps; ++k) {
            acc = __haydn_mula64_ss_ll(acc, (long long)hcoef[k],
                                       (long long)xsamp[n + ntaps - 1 - k]);
        }
        /* Q62 -> Q31: arithmetic shift + saturation. */
        ysamp[n] = __haydn_satsr64(acc, 31);
    }
}

int main(void) {
    fir_convol32x32(h, x_in, y_out, NTAPS, NOUT);
    return y_out[0];
}
