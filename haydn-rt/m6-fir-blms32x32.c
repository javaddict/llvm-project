//===-- m6-fir-blms32x32.c - LMS adaptive FIR port ------------------------===//
//
// NatureDSP port: fir_blms32x32 (block LMS adaptive filter) -> Haydn.
//   y[n]    = sum_k h[k] * x[n+k]                    (FIR output)
//   e[n]    = d[n] - y[n]                            (error)
//   h[k]   += mu * e[n] * x[n+k] / NTAPS             (tap update)
// The NatureDSP source uses AE_MULAF32RAS / AE_L32_IP / AE_S32L_IP. The Haydn
// mapping uses MUL64_SS_LL (signed*signed low-low 64-bit product) for both the
// FIR and the tap-update; the accumulator form MULA64_SS_LL folds the FIR sum
// back into the accumulator without a reload. mu and 1/NTAPS are folded into a
// single Q31 step coefficient.
//
// Validates the MULA64_SS_LL 3-arg accumulator MAC path end-to-end through a
// realistic two-loop adaptive kernel.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define NTAPS 4
#define NBLK  4

int h[NTAPS]      = { 0, 0, 0, 0 };
int xref[NBLK + NTAPS] = {
    0x10000000, 0x08000000, 0x04000000, 0x02000000,
    0x40000000, 0x20000000, 0x10000000, 0x08000000,
};
int dref[NBLK] = { 0x10000000, 0x20000000, 0x04000000, 0x08000000 };
int yout[NBLK];
int eout[NBLK];
int step = 0x01000000; /* Q31 mu/NTAPS */

void fir_blms32x32(int *restrict hcoefs, const int *restrict x,
                   const int *restrict d, int *restrict y, int *restrict e,
                   int ntaps, int nblk, int mu_step) {
    for (int n = 0; n < nblk; ++n) {
        /* FIR output: y[n] = sum_k h[k]*x[n+k] (Q31). */
        long long acc = 0;
        for (int k = 0; k < ntaps; ++k) {
            acc = __haydn_mula64_ss_ll(acc, (long long)hcoefs[k],
                                       (long long)x[n + k]);
        }
        int yn = __haydn_satsr64(acc, 31);
        int en = d[n] - yn;
        y[n] = yn;
        e[n] = en;
        /* Tap update: h[k] += (mu * e[n] * x[n+k]) >> 31.
         * First compute g = mu*e[n] (one MUL), then acc-form h update. */
        long long ge = __haydn_mul64_ss_ll((long long)mu_step, (long long)en);
        for (int k = 0; k < ntaps; ++k) {
            long long upd = __haydn_mul64_ss_ll(ge, (long long)x[n + k]);
            hcoefs[k] += __haydn_satsr64(upd, 31);
        }
    }
}

int main(void) {
    fir_blms32x32(h, xref, dref, yout, eout, NTAPS, NBLK, step);
    return yout[0];
}
