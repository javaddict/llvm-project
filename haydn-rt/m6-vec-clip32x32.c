//===-- m6-vec-clip32x32.c - clipping/saturation port ---------------------===//
//
// NatureDSP port: clipping kernel (clip x to [lo, hi]) -> Haydn. The
// NatureDSP idiom (vec_clip32x32 / saturate macros) compares against bounds
// and conditionally moves the bound into the result. We map this to the
// wave-5 SLT64+MOVT64 conditional-move pair twice:
//   r = (x < lo) ? lo : x   ;   r = (r > hi) ? hi : r
//
// Validates SLT64/MOVT64/MOVF64 in a double-clipping loop (compare-conditional-
// move executed 2N times).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { -100, 10, 200, 50, -500, 0, 700, 30 };
int z[N];
int lo_val = 0;
int hi_val = 100;

static inline int clip32(int x, int lo, int hi) {
    long long acc = (long long)x;
    /* Lower clip: if (x < lo) r = lo. */
    __haydn_slt64((long long)x);          /* SFR = (x < acc)?1:0 */
    acc = __haydn_movt64((long long)lo);  /* if SFR=1 (x<lo), acc=lo */
    /* Upper clip: if (acc > hi) r = hi.
     * SLT64(hi): SFR = (hi < acc)?1:0. If SFR=1, acc>hi, move hi. */
    __haydn_slt64((long long)hi);
    acc = __haydn_movt64((long long)hi);
    return (int)acc;
}

void vec_clip32x32(int *restrict zout, const int *restrict xin,
                   int lo, int hi) {
    for (int i = 0; i < N; ++i) {
        zout[i] = clip32(xin[i], lo, hi);
    }
}

int main(void) {
    vec_clip32x32(z, x, lo_val, hi_val);
    return z[0];
}
