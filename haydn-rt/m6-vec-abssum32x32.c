//===-- m6-vec-abssum32x32.c - vec abssum (L1-norm) port ------------------===//
//
// NatureDSP port: vector L1-norm sum(|x[i]|) -> Haydn. Uses ABS32S for the
// per-element saturating absolute value, then accumulates into a 64-bit
// scalar. The NatureDSP source is vec_eleabs32x32 + dot1; the Haydn mapping
// is ABS32S followed by MULA64_SS_LL (or scalar add). Exercises both the
// saturating-absolute path and the 64-bit accumulator MAC path.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { -1, 2, -3, 4, -5, 6, -7, 8 };

long long vec_abssum32x32(const int *restrict xin, int n) {
    long long acc = 0;
    for (int i = 0; i < n; ++i) {
        int a = __haydn_abs32s(xin[i]);
        acc = __haydn_mula64_ss_ll(acc, (long long)a, 1LL);
    }
    return acc;
}

int main(void) {
    long long s = vec_abssum32x32(x, N);
    return (int)s;
}
