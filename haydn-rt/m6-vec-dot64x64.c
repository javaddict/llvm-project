//===-- m6-vec-dot64x64.c - MAC-heavy dot product port --------------------===//
//
// NatureDSP port: vec_dot64x64 -> Haydn. Pure accumulator-MAC kernel that
// validates the D99 3-arg accumulator MAC fix end-to-end. Computes the
// dot product of two N-element i64 vectors using MULA64_SS_LL (low-low
// signed*signed -> 64-bit accumulator). This is the canonical dot-product
// kernel that was blocked before D99 and is now unblocked.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

long long x_in[N] = { 1, 2, 3, 4, 5, 6, 7, 8 };
long long y_in[N] = { 10, 20, 30, 40, 50, 60, 70, 80 };

/* Pure low-low signed*signed MAC chain. Each MULA64_SS_LL reads the prior
 * accumulator as its first operand (D99 3-arg form). */
long long vec_dot64x64(const long long *restrict x, const long long *restrict y,
                       int n) {
    long long acc = 0;
    for (int i = 0; i < n; ++i) {
        acc = __haydn_mula64_ss_ll(acc, x[i], y[i]);
    }
    return acc;
}

int main(void) {
    long long dot = vec_dot64x64(x_in, y_in, N);
    /* Return low 32 bits for debug inspection. */
    return (int)dot;
}
