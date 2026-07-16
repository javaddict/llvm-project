//===-- m6-vec-neg32x32.c - vec_neg32x32 (saturating negate) port ---------===//
//
// NatureDSP port: vec_neg32x32 -> Haydn. Elementwise saturating 32-bit
// negate. Each element maps to __haydn_neg32s, exercising the saturating-
// negate scalar codegen path. Pure arithmetic (no MAC, no float).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { 1, -2, 3, -4, 5, -6, 7, 0x80000000 };
int z[N];

void vec_neg32x32(int *restrict zout, const int *restrict xin) {
    for (int i = 0; i < N; ++i) {
        zout[i] = __haydn_neg32s(xin[i]);
    }
}

int main(void) {
    vec_neg32x32(z, x);
    return z[0];
}
