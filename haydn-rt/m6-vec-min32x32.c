//===-- m6-vec-min32x32.c - vec_min32x32 port -----------------------------===//
//
// NatureDSP port: vec_min32x32 -> Haydn. Elementwise minimum of two i32
// vectors. Maps each lane's min() to the wave-5 SLT64+MOVT64 conditional-move
// pair: SLT64(a) sets SFR=(a<b), then MOVT64(b) yields b if true else a.
// This is the canonical compare->SFR->conditional-move pattern and exercises
// the now-encoded SLT64/MOVT64 path end-to-end.
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 5, 9, 2, 7, 1, 8, 3, 6 };
int y[N] = { 4, 2, 6, 5, 9, 1, 8, 7 };
int z[N];

/* Elementwise min via SLT64/MOVT64: if (a<b) result=b else result=a would be
 * max; min selects the smaller, so we conditionally move when a>b. We model it
 * as: r=a; SLT64(b) [SFR=(b<a)]; MOVT64(b) -> r = (b<a)? b : a. */
static inline int min_sfr(int a, int b) {
    long long acc = (long long)a;
    __haydn_slt64((long long)b);   /* SFR = (b < acc) ? 1111 : 0000 */
    acc = __haydn_movt64((long long)b);
    return (int)acc;
}

void vec_min32x32(int *restrict zout,
                  const int *restrict xin,
                  const int *restrict yin) {
    for (int i = 0; i < N; ++i) {
        zout[i] = min_sfr(xin[i], yin[i]);
    }
}

int main(void) {
    vec_min32x32(z, x, y);
    return z[0];
}
