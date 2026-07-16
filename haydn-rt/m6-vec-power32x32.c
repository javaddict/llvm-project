//===-- m6-vec-power32x32.c - vector power/L2-norm port -------------------===//
//
// NatureDSP port: vec_power32x32 / vec_dot-style L2 norm -> Haydn. Computes
// sum(x[i]^2) for an i32 vector using MUL64_SS_LL (signed*signed low-low
// 64-bit multiply) then add. This is the simplest MAC-class kernel: pure
// multiply (no accumulator-form) chained with scalar add. Validates the
// MUL64_SS_LL 2-arg MAC code path that underlies MULA64_SS_LL.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 1, 2, 3, 4, 5, 6, 7, 8 };

long long vec_power32x32(const int *restrict xin, int n) {
    long long acc = 0;
    for (int i = 0; i < n; ++i) {
        long long prod = __haydn_mul64_ss_ll((long long)xin[i],
                                             (long long)xin[i]);
        acc += prod;
    }
    return acc;
}

int main(void) {
    long long p = vec_power32x32(x, N);
    return (int)p;
}
