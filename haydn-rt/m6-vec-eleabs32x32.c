//===-- m6-vec-eleabs32x32.c - vec_eleabs32x32 port -----------------------===//
//
// NatureDSP port: vec_eleabs32x32 -> Haydn. Elementwise saturating absolute
// value of an i32 vector. Maps each scalar ABS to __haydn_abs32s. This
// exercises the saturating-absolute codegen path (no MAC, no float).
//
// Pipeline: clang -target haydn-unknown-elf -c -> ELF .o -> lld -> runnable ELF.
//===----------------------------------------------------------------------===//

#include "haydn_intrin.h"

#define N 8

int x[N] = { -1, 2, -3, 4, -5, 6, -7, 8 };
int z[N];

void vec_eleabs32x32(int *restrict zout, const int *restrict xin) {
    for (int i = 0; i < N; ++i) {
        zout[i] = __haydn_abs32s(xin[i]);
    }
}

int main(void) {
    vec_eleabs32x32(z, x);
    return z[0];
}
