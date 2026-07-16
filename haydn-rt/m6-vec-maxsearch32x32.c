//===-- m6-vec-maxsearch32x32.c - vec_max search port ---------------------===//
//
// NatureDSP port: vec_max32x32 (reduce-scan to find max) -> Haydn. Walks an
// i32 vector keeping the running maximum in a scalar register, updating with
// the SLT64+MOVT64 conditional-move pair when a larger element is found.
// (Differs from m6-vec-max32x32.c which did elementwise pairwise max; this is
// the reduce-to-scalar form NatureDSP actually exposes as vec_max32x32.)
//
// Validates SLT64+MOVT64 in a reduction loop (compare against a live maximum).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 3, 9, 2, 7, 5, 1, 8, 4 };

int vec_maxsearch32x32(const int *restrict xin, int n) {
    int best = xin[0];
    for (int i = 1; i < n; ++i) {
        int cand = xin[i];
        long long acc = (long long)best;
        __haydn_slt64((long long)cand); /* SFR = (cand < best)?1:0 */
        /* If cand > best, SFR=0 -> we want to move cand into acc.
         * MOVT64 only moves when SFR=1, so here we want MOVF64(cand). */
        acc = __haydn_movf64((long long)cand);
        best = (int)acc;
    }
    return best;
}

int main(void) {
    int m = vec_maxsearch32x32(x, N);
    return m;
}
